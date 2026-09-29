import assert from 'node:assert/strict';
import { canPrintQueuePlate } from '../src/features/device-page/print-queue/types.ts';

assert.equal(canPrintQueuePlate({
  id: '9007199254740993',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'pending',
  plateIndex: 1,
}), true);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: '',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'pending',
  plateIndex: 1,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '0',
  deviceId: 'device-id',
  available: true,
  status: 'pending',
  plateIndex: 1,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: false,
  status: 'pending',
  plateIndex: 1,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'pending',
  plateIndex: 1.5,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'pending',
  plateIndex: 256,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'printing',
  plateIndex: 1,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'pending',
  plateIndex: 0,
}), false);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'failed',
  plateIndex: 1,
}), true);

assert.equal(canPrintQueuePlate({
  id: '1',
  modelId: 'model-id',
  profileId: '42',
  deviceId: 'device-id',
  available: true,
  status: 'completed',
  plateIndex: 1,
}), false);
