#!/usr/bin/env node
// Mirror @l10n/<locale>/docs/** into docs/<locale>/** so VitePress can pick
// them up under a single srcDir. Source of truth stays at @l10n/.
//
// Locales handled here must match `locales` in docs/.vitepress/config.mts.

import { cp, mkdir, rm, stat } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..');

const LOCALES = ['ru', 'zh-CN'];

async function exists(path) {
  try {
    await stat(path);
    return true;
  } catch {
    return false;
  }
}

async function syncLocale(locale) {
  const src = resolve(repoRoot, '@l10n', locale, 'docs');
  const dst = resolve(repoRoot, 'docs', locale);

  if (!(await exists(src))) {
    console.warn(`[sync-l10n] skip ${locale}: missing ${src}`);
    return;
  }

  await rm(dst, { recursive: true, force: true });
  await mkdir(dirname(dst), { recursive: true });
  await cp(src, dst, { recursive: true });
  console.log(`[sync-l10n] ${locale}: ${src} -> ${dst}`);
}

for (const locale of LOCALES) {
  await syncLocale(locale);
}
