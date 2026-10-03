'use strict';
const assert = require('node:assert/strict');
const { JsonLines, SerialLink } = require('../dashboard/serial.js');
(async () => {
  const got = [], noise = [];
  const parser = new JsonLines(v => got.push(v), v => noise.push(v), 80);
  parser.push('{"type":"GAME_'); parser.push('STATE","name":"Jyothi"}\n{"type":"ERROR"}\r\nboot noise\n');
  assert.deepEqual(got.map(v => v.type), ['GAME_STATE', 'ERROR']); assert.equal(noise.length, 1);
  parser.push('x'.repeat(100)); parser.push('\n{"type":"RECOVERED"}\n'); assert.equal(got.at(-1).type, 'RECOVERED');
  parser.push('null\n[]\n{"invalid":true}\n'); assert.equal(got.length, 3);
  const writes = [];
  const link = new SerialLink(() => {}, () => {});
  link.writer = { write: async value => { writes.push(new TextDecoder().decode(value)); } };
  await Promise.all([link.send({ type: 'RESET_GAME' }), link.send({ type: 'GET_STATUS' })]);
  assert.deepEqual(writes, ['{"type":"RESET_GAME"}\n', '{"type":"GET_STATUS"}\n']);
  link.writer = null; await assert.rejects(link.send({ type: 'GET_STATUS' }));
  console.log('PASS: partial/multiple/oversized/malformed JSON lines, recovery, ordered writes, disconnected writes');
})().catch(error => { console.error(error); process.exitCode = 1; });
