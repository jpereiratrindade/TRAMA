import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const css = readFileSync(resolve(root, 'web/src/style.css'), 'utf8');
const app = readFileSync(resolve(root, 'web/src/main.ts'), 'utf8');

const checks = [
  ['CSS variables are valid at the stylesheet root', css.startsWith(':root{')],
  ['the application grid allows its main column to shrink', css.includes('.shell>main{min-width:0}')],
  ['filters use responsive CSS Grid', css.includes('.filters{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,180px),1fr))')],
  ['filter children cannot force column overflow', css.includes('.filters>*{min-width:0}')],
  ['form controls stay inside their grid track', css.includes('.filters select,.filters input{display:block;width:100%;min-width:0;max-width:100%')],
  ['data cards use shrink-safe tracks', css.includes('grid-template-columns:repeat(4,minmax(0,1fr))')],
  ['small screens collapse to one card per row', css.includes('.cards{grid-template-columns:1fr}')],
  ['reduced-motion preferences are respected', css.includes('@media(prefers-reduced-motion:reduce)')],
  ['interactive controls meet the 44px enhanced target', css.includes('button,.button,input,select,textarea{min-height:44px')],
  ['keyboard focus is visibly styled', css.includes('a:focus-visible{outline:3px solid')],
  ['a keyboard skip link is present', app.includes('class="skip-link" href="#content"')],
  ['navigation exposes its accessible name', app.includes('<nav aria-label="Navegação principal">')],
  ['mobile menu exposes state and ownership', app.includes('aria-controls="sidebar" aria-expanded="false"')],
  ['current navigation state is announced', app.includes("setAttribute('aria-current', 'page')")],
  ['filter groups have accessible names', app.includes('aria-label="Filtros da visão geral"') && app.includes('aria-label="Filtros do relatório"')]
];

for (const [message, passed] of checks) assert.ok(passed, message);
console.log(`PASS: ${checks.length} responsive layout and accessibility contracts`);
