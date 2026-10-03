"""Real Chrome, real dashboard, explicitly mocked Web Serial; no hardware claim."""
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
MOCK = r"""
(() => {
  let controller, opened = false;
  window.testCommands = [];
  window.testSilent = false;
  window.testWrongBoard = false;
  window.testState = {type:'GAME_STATE', uptimeMs:20000, epoch:7, gameId:0,
    sequence:0, state:'IDLE', currentPlayer:0, previousPlayer:0, targetPlayer:0,
    paused:false, espnow:true, errors:0, sendFailures:0, invalidPackets:0, queueDrops:0,
    lastPacketMs:19900, lastAckMs:19800,
    players:Array.from({length:8},(_,i)=>({id:i+1,online:'ONLINE',resetAck:true,state:'WAITING'}))};
  window.testPush = data => {
    if(!opened || window.testSilent) return;
    const bytes = new TextEncoder().encode(JSON.stringify(data)+'\n');
    controller.enqueue(bytes.slice(0,11)); controller.enqueue(bytes.slice(11));
  };
  window.testUnplug = () => { opened=false; controller.error(new Error('USB device removed')); };
  const port = {
    async open() {
      opened=true;
      this.readable=new ReadableStream({start(c){controller=c;},cancel(){opened=false;}});
      this.writable=new WritableStream({write(bytes){
        const cmd=JSON.parse(new TextDecoder().decode(bytes)); window.testCommands.push(cmd);
        if(cmd.type==='GET_CONFIG') {
          window.testPush({type:'CONFIG',deviceId:window.testWrongBoard?3:1,role:window.testWrongBoard?'SLAVE':'MASTER',channel:1,players:window.PLAYERS});
          window.testPush(window.testState);
        }
        if(cmd.type==='GET_STATUS') window.testPush(window.testState);
        if(cmd.type==='START_GAME') {
          Object.assign(window.testState,{state:'ACTIVE',gameId:7,currentPlayer:cmd.player,previousPlayer:1,targetPlayer:cmd.player,sequence:1});
          window.testPush({type:'BALL_RECEIVED',from:1,to:cmd.player,message:'Ball activation acknowledged'});
          window.testPush(window.testState);
        }
        if(cmd.type==='RESET_GAME') {
          Object.assign(window.testState,{state:'IDLE',gameId:0,currentPlayer:0,targetPlayer:0,sequence:0});
          window.testPush(window.testState);
        }
      }});
    },
    async close(){opened=false;}
  };
  Object.defineProperty(navigator,'serial',{value:{requestPort:async()=>port}, configurable:true});
})();
"""

def main():
    (ROOT / "build").mkdir(exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome", headless=True)
        page = browser.new_page(viewport={"width": 1440, "height": 1050}, device_scale_factor=1)
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.add_init_script(MOCK)
        page.goto((ROOT / "dashboard/index.html").as_uri())
        expect(page.locator(".player")).to_have_count(8)
        expect(page.locator("#start")).to_be_disabled()
        page.locator("#connect").click()
        expect(page.locator("#connection")).to_contain_text("Master connected")
        expect(page.locator("#online")).to_have_text("8")
        expect(page.locator("#start")).to_be_enabled()
        page.locator("#starting-player").select_option("4")
        page.locator("#start").click()
        expect(page.locator("#holder")).to_have_text("Durgamani R")
        expect(page.locator(".player.active")).to_have_count(1)
        expect(page.locator('.player[data-id="4"]')).to_have_class("player online active")
        page.screenshot(path=str(ROOT / "build/dashboard-desktop.png"), full_page=True)
        page.evaluate("Object.assign(testState,{currentPlayer:3,previousPlayer:4,sequence:2}); testPush(testState)")
        expect(page.locator("#holder")).to_have_text("ABHISHEK KUMAR")
        expect(page.locator(".player.active")).to_have_count(1)
        page.evaluate("testState.players[2].online='OFFLINE'; testPush(testState)")
        expect(page.locator(".player.active")).to_have_count(0)
        expect(page.locator("#holder")).to_have_text("Transfer / state uncertain")
        page.evaluate("testState.players[2].online='ONLINE'; testPush(testState)")
        page.set_viewport_size({"width": 390, "height": 844})
        page.screenshot(path=str(ROOT / "build/dashboard-mobile.png"), full_page=True)
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), "Horizontal overflow"
        page.set_viewport_size({"width": 1440, "height": 1050})
        page.evaluate("testSilent=true")
        expect(page.locator("#reset")).to_be_disabled(timeout=7000)
        expect(page.locator(".player.active")).to_have_count(0)
        page.evaluate("testSilent=false; testPush(testState)")
        expect(page.locator("#reset")).to_be_enabled()
        page.locator("#reset").click()
        expect(page.locator("#holder")).to_have_text("Waiting to play")
        expect(page.locator("#start")).to_be_enabled()
        page.locator("#connect").click()
        expect(page.locator("#connection")).to_contain_text("Master disconnected")
        page.locator("#connect").click()
        expect(page.locator("#connection")).to_contain_text("Master connected")
        page.evaluate("testUnplug()")
        expect(page.locator("#connection")).to_contain_text("Master disconnected")
        page.locator("#connect").click()
        expect(page.locator("#connection")).to_contain_text("Master connected")
        page.locator("#connect").click()
        page.evaluate("testWrongBoard=true")
        page.locator("#connect").click()
        expect(page.locator("#connection")).to_contain_text("Master disconnected")
        expect(page.locator("#start")).to_be_disabled()
        assert not errors, errors
        browser.close()
    print("PASS: Chrome rendering, fragmented serial, start/holder/reset, offline/stale status, disconnect/unplug/reconnect, wrong board, mobile layout; no page errors")

if __name__ == "__main__":
    main()
