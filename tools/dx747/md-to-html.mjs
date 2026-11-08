#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// A small Markdown to HTML converter for the SMK-37 manual (docs/manual-smk37/manual.md), for machines without pandoc. It knows only what that file
// uses, and says so when it meets something else:
//   front matter (title, subtitle, author, lang) between --- lines; headings # and ##; paragraphs; - and 1. lists (no nesting); pipe tables;
//   ![caption](image){.class} on a line of its own (the image is embedded as a data: address); ::: {.class} ... ::: blocks; and inline
//   **bold**, *italic*, `code`, [text](address) and <address>.
// usage: node tools/dx747/md-to-html.mjs manual.md manual.css out.html
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';

const [source, cssFile, out] = process.argv.slice(2);
if (!source || !cssFile || !out) { console.error('usage: md-to-html.mjs manual.md manual.css out.html'); process.exit(2); }
const base = dirname(resolve(source));
const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const fail = (msg) => { console.error('md-to-html: ' + msg); process.exit(1); };

/** The id pandoc gives a heading: from the first letter on, lower case, spaces to hyphens, no punctuation but _ - . */
const slug = (text) => text.replace(/[`*]/g, '').toLowerCase().replace(/^[^a-z]+/, '').replace(/[^a-z0-9_. -]/g, '').trim().replace(/\s+/g, '-');

function inline(text) {
  const codes = [];
  let s = text.replace(/`([^`]+)`/g, (_, c) => { codes.push(c); return '\u0000' + (codes.length - 1) + '\u0000'; });
  s = esc(s);
  s = s.replace(/&lt;((?:https?:\/\/)[^\s&]+)&gt;/g, (_, u) => `<a href="${u}">${u}</a>`);
  s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (_, t, u) => `<a href="${u}">${t}</a>`);
  s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>').replace(/(^|[^*])\*([^*\s][^*]*)\*/g, '$1<em>$2</em>');
  return s.replace(/\u0000(\d+)\u0000/g, (_, i) => `<code>${esc(codes[Number(i)])}</code>`);
}

function image(alt, src, cls) {
  const path = resolve(base, src);
  let data;
  try { data = readFileSync(path); } catch { fail('image not found: ' + src); }
  const type = /\.png$/i.test(src) ? 'image/png' : /\.jpe?g$/i.test(src) ? 'image/jpeg' : fail('image type: ' + src);
  const tag = `<img${cls ? ` class="${cls}"` : ''} alt="${esc(alt)}" src="data:${type};base64,${data.toString('base64')}">`;
  return cls === 'cover' ? tag : `<figure>${tag}${alt ? `<figcaption>${inline(alt)}</figcaption>` : ''}</figure>`;
}

function blocks(lines) {
  const html = [];
  for (let i = 0; i < lines.length;) {
    const line = lines[i];
    if (!line.trim()) { i++; continue; }
    let m;
    if ((m = /^::: \{\.([a-z-]+)\}\s*$/.exec(line))) {
      const body = []; i++;
      while (i < lines.length && lines[i].trim() !== ':::') body.push(lines[i++]);
      if (i >= lines.length) fail('a ::: block is not closed');
      i++; html.push(`<div class="${m[1]}">${blocks(body)}</div>`); continue;
    }
    if ((m = /^(#{1,2}) (.+)$/.exec(line))) { const n = m[1].length; html.push(`<h${n} id="${slug(m[2])}">${inline(m[2])}</h${n}>`); i++; continue; }
    if ((m = /^!\[([^\]]*)\]\(([^)]+)\)(?:\{\.([a-z]+)\})?\s*$/.exec(line))) { html.push(image(m[1], m[2], m[3])); i++; continue; }
    if (/^\|/.test(line)) {
      const rows = [];
      while (i < lines.length && /^\|/.test(lines[i])) rows.push(lines[i++].trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim()));
      if (rows.length < 2 || !rows[1].every((c) => /^:?-+:?$/.test(c))) fail('a table needs a header and a --- line: ' + line.slice(0, 40));
      const width = rows[0].length;
      for (const r of rows) if (r.length !== width) fail('a table row has ' + r.length + ' cells, not ' + width + ': ' + r[0].slice(0, 40));
      html.push('<table><thead><tr>' + rows[0].map((c) => `<th>${inline(c)}</th>`).join('') + '</tr></thead><tbody>' +
        rows.slice(2).map((r) => '<tr>' + r.map((c) => `<td>${inline(c)}</td>`).join('') + '</tr>').join('') + '</tbody></table>');
      continue;
    }
    if (/^(- |\d+\. )/.test(line)) {
      const ordered = /^\d/.test(line), items = [];
      while (i < lines.length && /^(- |\d+\. )/.test(lines[i])) {
        let item = lines[i++].replace(/^(- |\d+\. )/, '');
        while (i < lines.length && /^ {2,}\S/.test(lines[i])) item += ' ' + lines[i++].trim();
        items.push(item);
      }
      html.push(`<${ordered ? 'ol' : 'ul'}>${items.map((t) => `<li>${inline(t)}</li>`).join('')}</${ordered ? 'ol' : 'ul'}>`); continue;
    }
    const para = [];
    while (i < lines.length && lines[i].trim() && !/^(#{1,2} |::: |\||- |\d+\. |!\[)/.test(lines[i])) para.push(lines[i++].trim());
    if (!para.length) fail('cannot read: ' + line.slice(0, 60));
    html.push(`<p>${inline(para.join(' '))}</p>`);
  }
  return html.join('\n');
}

let text = readFileSync(source, 'utf8').replace(/\r\n/g, '\n');
const meta = {};
const fm = /^---\n([\s\S]*?)\n---\n/.exec(text);
if (fm) {
  text = text.slice(fm[0].length);
  for (const l of fm[1].split('\n')) { const m = /^(\w+): "?(.*?)"?$/.exec(l); if (m) meta[m[1]] = m[2]; }
}
const head = fm ? `<header id="title-block-header"><h1 class="title">${esc(meta.title ?? '')}</h1><p class="subtitle">${esc(meta.subtitle ?? '')}</p><p class="author">${esc(meta.author ?? '')}</p></header>\n` : '';
const page = `<!doctype html>\n<html lang="${meta.lang ?? 'en'}"><head><meta charset="utf-8"><title>${esc(meta.pagetitle ?? meta.title ?? '')}</title>` +
  `<style>\n${readFileSync(cssFile, 'utf8')}</style></head><body>\n${head}${blocks(text.split('\n'))}\n</body></html>\n`;
writeFileSync(out, page);
