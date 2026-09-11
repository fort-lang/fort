'use strict';

// The per-file memory of the last successful check: bounded, and ordered.
//
// An editor session saves hundreds of files over a day and each check answers
// about a whole closure, so a map keyed by file grows without bound unless it
// is capped. A Map iterates in insertion order, so the entry written longest
// ago is the one to drop.

// `value` under `key`, with the oldest entries evicted until the map holds at
// most `limit`. Rewriting a key moves it to the end, so a file checked again is
// not the next one evicted.
function put(map, key, value, limit) {
  map.delete(key);
  map.set(key, value);
  while (map.size > limit) {
    const oldest = map.keys().next().value;
    map.delete(oldest);
  }
  return map;
}

// The generation of a run, remembered per file, with the cap applied only to
// the entries of older runs: one check answers about a whole closure, and a
// closure larger than the cap would otherwise evict, inside the loop that
// writes them, the entries this very run just wrote -- which would let a slower
// older run republish its stale diagnostics over them.
function putGeneration(generations, key, generation, limit) {
  generations.delete(key);
  generations.set(key, generation);
  while (generations.size > limit) {
    const oldest = generations.keys().next().value;
    if (generations.get(oldest) === generation) break;
    generations.delete(oldest);
  }
  return generations;
}

// Whether a run's answer about `key` is newer than the one already published.
// Runs are started in order and finish in any order, so two saves of different
// files whose closures overlap would otherwise let the older document be
// published last and overwrite the newer answer.
function isNewer(generations, key, generation) {
  const published = generations.get(key);
  return published === undefined || generation > published;
}

// Whether an answer is about the text the buffer holds now. `checked` is what
// `bufferStateOf` recorded when the compiler read the file: null when no window
// had it open, so nothing is known about what is on screen now. A buffer that
// held unsaved edits at check time was not what the compiler read either.
function isFresh(checked, version) {
  if (!checked || checked.dirty) return false;
  return checked.version === version;
}

module.exports = { isFresh, isNewer, put, putGeneration };
