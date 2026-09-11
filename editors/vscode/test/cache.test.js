'use strict';

// The bounded per-file memory and the generation guard that keeps an older
// answer from overwriting a newer one.

const test = require('node:test');
const assert = require('node:assert/strict');

const cache = require('../lib/cache');

test('a map keeps at most the limit, dropping what was written longest ago', () => {
  const map = new Map();
  for (const key of ['a', 'b', 'c', 'd']) cache.put(map, key, key.toUpperCase(), 3);
  assert.deepEqual([...map.keys()], ['b', 'c', 'd']);
  assert.equal(map.get('d'), 'D');
});

test('rewriting a key moves it to the end rather than adding one', () => {
  const map = new Map();
  cache.put(map, 'a', 1, 2);
  cache.put(map, 'b', 2, 2);
  cache.put(map, 'a', 3, 2);
  assert.deepEqual([...map.keys()], ['b', 'a']);
  assert.equal(map.size, 2);
  cache.put(map, 'c', 4, 2);
  assert.deepEqual([...map.keys()], ['a', 'c']);
});

test('a limit of one keeps only the last, and a large limit evicts nothing', () => {
  const one = new Map();
  cache.put(one, 'a', 1, 1);
  cache.put(one, 'b', 2, 1);
  assert.deepEqual([...one.keys()], ['b']);
  const many = new Map();
  for (let i = 0; i < 10; i += 1) cache.put(many, String(i), i, 512);
  assert.equal(many.size, 10);
});

// A closure larger than the cap must not evict, while it is being published,
// the entries the same run has just written: the older run that is still in
// flight would then be free to republish over them.
test('the cap never evicts an entry of the run that is writing', () => {
  const generations = new Map();
  for (let i = 0; i < 6; i += 1) cache.putGeneration(generations, 'f' + i, 7, 3);
  assert.equal(generations.size, 6);
  assert.deepEqual([...new Set(generations.values())], [7]);
  // The next run evicts the oldest entries of the run before it, down to the
  // cap, and then stops at its own.
  for (let i = 0; i < 2; i += 1) cache.putGeneration(generations, 'g' + i, 8, 3);
  assert.equal(generations.has('g0'), true);
  assert.equal(generations.has('g1'), true);
  assert.equal(generations.has('f0'), false);
  assert.equal(generations.size, 3);
});

test('an unseen file is newer, and a later generation wins', () => {
  const generations = new Map();
  assert.equal(cache.isNewer(generations, '/a.ft', 1), true);
  generations.set('/a.ft', 7);
  assert.equal(cache.isNewer(generations, '/a.ft', 8), true);
  assert.equal(cache.isNewer(generations, '/a.ft', 7), false);
  assert.equal(cache.isNewer(generations, '/a.ft', 6), false);
});

// A client must not present an answer as fresh when it cannot know that it is
// (toolchain.md 9.2), so only a buffer the compiler's own text came from
// counts.
test('an answer is fresh only for the very text that was checked', () => {
  assert.equal(cache.isFresh({ version: 3, dirty: false }, 3), true);
  assert.equal(cache.isFresh({ version: 3, dirty: false }, 4), false);
  assert.equal(cache.isFresh({ version: 3, dirty: true }, 3), false);
  // No window had the file open when it was checked, so nothing is known about
  // what is on screen now.
  assert.equal(cache.isFresh(null, 3), false);
  assert.equal(cache.isFresh(undefined, 3), false);
  assert.equal(cache.isFresh({ version: 3, dirty: false }, undefined), false);
});

// The case the guard exists for: two saves whose closures share a file, the
// older run finishing last.
test('an out-of-order run does not republish a file the newer one covered', () => {
  const generations = new Map();
  const published = new Map();
  const publish = (file, generation, items) => {
    if (!cache.isNewer(generations, file, generation)) return;
    generations.set(file, generation);
    published.set(file, items);
  };
  publish('/shared.ft', 2, ['fresh']);
  publish('/shared.ft', 1, ['stale']);
  publish('/only-in-the-old-closure.ft', 1, ['kept']);
  assert.deepEqual(published.get('/shared.ft'), ['fresh']);
  assert.deepEqual(published.get('/only-in-the-old-closure.ft'), ['kept']);
});
