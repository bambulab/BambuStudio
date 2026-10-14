#!/usr/bin/env node
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const [profile, portText] = process.argv.slice(2);
const sdkRoot = process.env.MCP_SDK_DIR;
assert(profile?.startsWith('/') && sdkRoot?.startsWith('/'),
  'Pass absolute disposable profile and MCP_SDK_DIR paths');
const port = Number(portText);
assert(Number.isInteger(port) && port >= 1024 && port <= 65535, 'Pass a valid port');
const marker = await readFile(join(profile, '.native_acceptance_profile'), 'utf8');
assert.equal(marker, 'disposable test profile\n');
const token = (await readFile(join(profile, 'mcp.token'), 'ascii')).trim();
assert.match(token, /^[0-9a-f]{64}$/);
const oracle = JSON.parse(await readFile(join(dirname(fileURLToPath(import.meta.url)), 'legacy_tools_oracle.json'), 'utf8'));
assert.equal(oracle.length, 90);

const { Client } = await import(pathToFileURL(join(sdkRoot, 'dist/esm/client/index.js')).href);
const { StreamableHTTPClientTransport } = await import(
  pathToFileURL(join(sdkRoot, 'dist/esm/client/streamableHttp.js')).href);
const client = new Client({ name: 'native-acceptance', version: '1.0' });
const transport = new StreamableHTTPClientTransport(new URL(`http://127.0.0.1:${port}/mcp`), {
  requestInit: { headers: { Authorization: `Bearer ${token}` } },
});
const deadline = setTimeout(() => {
  process.stderr.write('External MCP SDK check timed out after 30 seconds\n');
  process.exit(1);
}, 30000);

try {
  await client.connect(transport);
  const listed = await client.listTools();
  assert.equal(listed.tools.length, 90, 'official SDK must discover every legacy tool');
  for (let i = 0; i < oracle.length; i++) {
    const actual = listed.tools[i];
    const expected = oracle[i];
    assert.equal(actual.name, expected.name);
    assert.equal(actual.description, expected.description, actual.name);
    assert.deepEqual(actual.inputSchema, expected.inputSchema, `${actual.name} schema`);
    assert.deepEqual(actual.annotations, expected.annotations, `${actual.name} annotations`);
  }
  const capabilities = await client.callTool({ name: 'app_get_capabilities', arguments: {} });
  assert(!capabilities.isError);
  const methods = capabilities.structuredContent?.methods;
  assert(Array.isArray(methods) && methods.length === 90);
  assert.deepEqual(new Set(methods.map(method => method.replaceAll('.', '_'))),
    new Set(oracle.map(tool => tool.name)), 'native method dispatch must cover all 90 tools');
  const model = await client.callTool({ name: 'model_list', arguments: {} });
  assert(!model.isError && Array.isArray(model.structuredContent?.objects));
  const plates = await client.callTool({ name: 'plate_list', arguments: {} });
  assert(!plates.isError && typeof plates.structuredContent?.plateRevision === 'string');
  process.stdout.write('PASS official MCP SDK discovered all 90 exact legacy contracts, native methods, and live model/plate reads\n');
} finally {
  try {
    await client.close();
  } finally {
    clearTimeout(deadline);
  }
}
