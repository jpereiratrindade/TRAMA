import './style.css';
import './map.css';
import 'leaflet/dist/leaflet.css';
import L from 'leaflet';

type J = Record<string, any>;
let csrf = '', project = '', projectName = 'Nenhum projeto selecionado';
const app = document.querySelector<HTMLDivElement>('#app')!;

const tramaMark = `<svg class="brand-mark" viewBox="0 0 64 64" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2.8"><path d="M32 5 55 18v28L32 59 9 46V18z"/><path d="M32 5v54M9 18l46 28M55 18 9 46"/></g><g fill="currentColor"><circle cx="32" cy="5" r="4"/><circle cx="55" cy="18" r="4"/><circle cx="55" cy="46" r="4"/><circle cx="32" cy="59" r="4"/><circle cx="9" cy="46" r="4"/><circle cx="9" cy="18" r="4"/><circle cx="32" cy="32" r="5"/></g></svg>`;

function brand() {
  return `<div class="brand">${tramaMark}<span class="brand-text"><small>Ecossistema SisTer</small><strong>TRAMA</strong></span></div>`;
}

async function api(path: string, init: RequestInit = {}) {
  const h = new Headers(init.headers);
  h.set('Content-Type', 'application/json');
  if (csrf) h.set('X-CSRF-Token', csrf);
  const r = await fetch(path, { ...init, headers: h });
  if (r.status === 401) {
    login();
    throw Error('Autenticação necessária');
  }
  if (!r.ok) {
    const j = await r.json().catch(() => ({ error: { message: 'Erro inesperado' } }));
    throw Error(j.error?.message || 'Erro');
  }
  return r.headers.get('content-type')?.includes('json') ? r.json() : r.text();
}

function login() {
  app.innerHTML = `<main class="login"><section class="login-story"><div>${brand()}<p class="eyebrow">Conhecimento que nasce no território</p><h1>Relatos que fortalecem a agricultura e a pecuária familiar.</h1><p>Registre atividades, preserve as vozes das comunidades e acompanhe os encaminhamentos construídos junto à FETAG.</p></div><div class="field-lines" aria-hidden="true"><i></i><i></i><i></i><i></i></div></section><section class="login-card"><span class="login-kicker">Acesso ao sistema</span><h2>Boas-vindas</h2><p>Entre para continuar o trabalho de campo.</p><form id="login"><label>Usuário<input name="login" autocomplete="username" required></label><label>Senha<input name="password" type="password" autocomplete="current-password" required></label><button>Entrar</button><p class="error" role="alert"></p></form><small>Sempre pronto. Sempre incompleto.</small></section></main>`;
  (document.querySelector('#login') as HTMLFormElement).onsubmit = async (e) => {
    e.preventDefault();
    try {
      const j = await api('/api/v1/auth/login', {
        method: 'POST',
        body: JSON.stringify(Object.fromEntries(new FormData(e.currentTarget as HTMLFormElement)))
      });
      csrf = j.csrf_token;
      await layout(j.user);
    } catch (x) {
      document.querySelector('.error')!.textContent = (x as Error).message;
    }
  };
}

const nav = [
  ['dashboard', 'Visão Geral'],
  ['projects', 'Projetos'],
  ['units', 'Territórios e Unidades'],
  ['activities', 'Atividades'],
  ['observations', 'Observações'],
  ['actions', 'Encaminhamentos'],
  ['reports', 'Relatórios'],
  ['sources', 'Fontes e Qualidade']
];

async function layout(user: J) {
  const ps = await api('/api/v1/projects');
  if (ps.data.length) {
    project = ps.data[0].id;
    projectName = ps.data[0].name;
  }
  app.innerHTML = `<a class="skip-link" href="#content">Ir para o conteúdo</a><div class="shell"><aside id="sidebar"><div class="sidebar-brand">${brand()}<span>Relatos do campo</span></div><nav aria-label="Navegação principal">${nav.map((x, i) => `<button data-page="${x[0]}" ${i ? '' : 'class="active" aria-current="page"'}><i aria-hidden="true"></i>${x[1]}</button>`).join('')}</nav><div class="sidebar-note"><small>Atuação territorial</small><strong>Agricultura e pecuária familiar</strong><span>Registros construídos junto à FETAG</span></div><footer><span class="user-dot" aria-hidden="true"></span>${h(user.login)}<button id="logout">Sair</button></footer></aside><main><header><button id="menu" aria-label="Abrir menu" aria-controls="sidebar" aria-expanded="false">☰</button><div><strong id="title">Visão Geral</strong><small id="project-name">${h(projectName)}</small></div><span class="badge"><i></i> Território RS conectado</span></header><div id="content" tabindex="-1"></div></main></div>`;
  
  document.querySelectorAll('[data-page]').forEach(b => (b as HTMLButtonElement).onclick = () => show((b as HTMLElement).dataset.page!));
  document.querySelector('#menu')!.addEventListener('click', (event) => {
    const open = document.querySelector('aside')!.classList.toggle('open');
    (event.currentTarget as HTMLButtonElement).setAttribute('aria-expanded', String(open));
  });
  document.querySelector('#logout')!.addEventListener('click', async () => {
    await api('/api/v1/auth/logout', { method: 'POST', body: '{}' });
    csrf = '';
    login();
  });
  show(project ? 'dashboard' : 'projects');
}

function view(title: string, html: string) {
  document.querySelector('#title')!.textContent = title;
  document.querySelector('#content')!.innerHTML = html;
  document.querySelector('aside')?.classList.remove('open');
}

function formData(form: HTMLFormElement) {
  return Object.fromEntries(new FormData(form)) as J;
}

function h(value: unknown) {
  return String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]!));
}

async function remove(path: string, back: string) {
  if (!confirm('Excluir este registro? O histórico de auditoria será preservado.')) return;
  await api(path, { method: 'DELETE', body: '{}' });
  await show(back);
}

async function show(page: string) {
  document.querySelectorAll('[data-page]').forEach(b => {
    const active = (b as HTMLElement).dataset.page === page;
    b.classList.toggle('active', active);
    if (active) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current');
  });
  view(nav.find(x => x[0] === page)?.[1] || page, '<p class="loading">Carregando…</p>');
  try {
    if (page === 'projects') await projects();
    else if (!project) view('Projetos', '<div class="notice">Crie um projeto para começar.</div>');
    else if (page === 'dashboard') await dashboard();
    else if (page === 'units') await units();
    else if (page === 'activities') await records('activities');
    else if (page === 'observations') await records('observations');
    else if (page === 'actions') await records('action-items');
    else if (page === 'reports') reports();
    else if (page === 'sources') await sources();
    else view('Página não encontrada', '<div class="notice">Esta área não está disponível.</div>');
  } catch (e) {
    view('Erro', `<div class="notice error">${(e as Error).message}</div>`);
  }
}

async function projects() {
  const j = await api('/api/v1/projects');
  view('Projetos', `<button id="new">Novo projeto</button><section class="panel" id="form"></section><section class="panel"><h2>Projetos</h2>${j.data.length ? j.data.map((x: J) => `<article class="record"><b>${h(x.name)}</b><span>${h(x.code)} · ${h(x.status)} · revisão ${x.revision}</span><p>${h(x.description)}</p><button data-select="${h(x.id)}" class="secondary">Selecionar</button> <button data-edit='${encodeURIComponent(JSON.stringify(x))}' class="secondary">Editar</button> <button data-delete="${h(x.id)}" class="danger">Excluir</button></article>`).join('') : '<p>Nenhum projeto cadastrado.</p>'}</section>`);
  
  const render = (x: J = {}) => {
    document.querySelector('#form')!.innerHTML = `<h2>${x.id ? 'Editar' : 'Novo'} projeto</h2><form id="project-form"><label>Código<input name="code" value="${h(x.code || '')}" required></label><label>Nome<input name="name" value="${h(x.name || '')}" required></label><label>Descrição<textarea name="description">${h(x.description || '')}</textarea></label><label>Status<select name="status"><option ${x.status === 'active' ? 'selected' : ''}>active</option><option ${x.status === 'paused' ? 'selected' : ''}>paused</option><option ${x.status === 'completed' ? 'selected' : ''}>completed</option></select></label><button>Salvar</button></form>`;
    (document.querySelector('#project-form') as HTMLFormElement).onsubmit = async (e) => {
      e.preventDefault();
      const body = { ...formData(e.currentTarget as HTMLFormElement), revision: x.revision };
      await api(x.id ? `/api/v1/projects/${x.id}` : '/api/v1/projects', {
        method: x.id ? 'PATCH' : 'POST',
        body: JSON.stringify(body)
      });
      await show('projects');
    };
  };

  document.querySelector('#new')!.addEventListener('click', () => render());
  document.querySelectorAll('[data-edit]').forEach(b => b.addEventListener('click', () => render(JSON.parse(decodeURIComponent((b as HTMLElement).dataset.edit!)))));
  document.querySelectorAll('[data-delete]').forEach(b => b.addEventListener('click', () => remove(`/api/v1/projects/${(b as HTMLElement).dataset.delete}`, 'projects')));
  document.querySelectorAll('[data-select]').forEach(b => b.addEventListener('click', () => {
    const x = j.data.find((v: J) => v.id === (b as HTMLElement).dataset.select);
    project = x.id;
    projectName = x.name;
    document.querySelector('#project-name')!.textContent = x.name;
    show('dashboard');
  }));
}

async function dashboard() {
  const [projectsRes, groupsRes, unitsRes, dimsRes] = await Promise.all([
    api('/api/v1/projects'),
    api(`/api/v1/project-groups?project_id=${project}`),
    api(`/api/v1/units?project_id=${project}`),
    api(`/api/v1/analytics/dimensions?project_id=${project}`)
  ]);

  const coredes: any[] = dimsRes.data?.coredes || [];
  const rfs: any[] = dimsRes.data?.functional_regions || [];
  const biomes: any[] = dimsRes.data?.biomes || [];

  view('Visão Geral', `
    <section class="welcome">
      <div><p class="eyebrow">Trabalho de base · FETAG</p><h1>O campo contado por quem vive nele.</h1><p>Acompanhe atividades, participação e encaminhamentos da agricultura e da pecuária familiar nos territórios.</p></div>
      <div class="welcome-mark" aria-hidden="true">${tramaMark}</div>
    </section>
    <section class="filters dashboard-filters" aria-label="Filtros da visão geral">
      <label>Projeto
        <select id="dash-project">${projectsRes.data.map((x: J) => `<option value="${h(x.id)}" ${x.id === project ? 'selected' : ''}>${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Grupo
        <select id="group"><option value="">Todos</option>${groupsRes.data.map((x: J) => `<option value="${h(x.id)}">${h(x.label)}</option>`).join('')}</select>
      </label>
      <label>Unidade
        <select id="unit"><option value="">Todas</option>${unitsRes.data.map((x: J) => `<option value="${h(x.id)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>COREDE
        <select id="corede"><option value="">Todos os 28 COREDEs</option>${coredes.map((x: J) => `<option value="${h(x.name)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Região Funcional
        <select id="rf"><option value="">Todas as 9 RFs</option>${rfs.map((x: J) => `<option value="${h(x.name)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Bioma
        <select id="biome"><option value="">Todos os Biomas</option>${biomes.map((x: J) => `<option value="${h(x.name)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <button id="reset" class="secondary">Limpar filtros</button>
    </section>
    <div id="dash"><p class="loading">Consultando dados locais e catálogo territorial…</p></div>
  `);

  const update = async () => {
    const g = (document.querySelector('#group') as HTMLSelectElement).value;
    const u = (document.querySelector('#unit') as HTMLSelectElement).value;
    const c = (document.querySelector('#corede') as HTMLSelectElement).value;
    const rf = (document.querySelector('#rf') as HTMLSelectElement).value;
    const b = (document.querySelector('#biome') as HTMLSelectElement).value;

    const q = `project_id=${encodeURIComponent(project)}&group_id=${encodeURIComponent(g)}&unit_id=${encodeURIComponent(u)}&corede=${encodeURIComponent(c)}&functional_region=${encodeURIComponent(rf)}&biome=${encodeURIComponent(b)}`;
    const [ov, series] = await Promise.all([
      api('/api/v1/analytics/overview?' + q),
      api('/api/v1/analytics/units?' + q)
    ]);

    const k = ov.kpis;
    const max = Math.max(1, ...series.data.map((x: J) => x.total));
    const pct = (v: any) => v === null ? '—' : Number(v).toLocaleString('pt-BR', { maximumFractionDigits: 1 }) + '%';

    document.querySelector('#dash')!.innerHTML = `
      <div class="provenance-box">
        <b>Dimensões territoriais integradas:</b> Região Funcional, COREDE, Município e contexto ecológico (Bioma Predominante e Ocorrentes).
      </div>
      <section class="cards">
        ${[
          ['Unidades documentadas', k.uacs_documented],
          ['Participações informadas', k.reported_attendances],
          ['Mulheres', k.reported_women],
          ['Homens', k.reported_men],
          ['Jovens', k.reported_youth],
          ['Participação feminina', pct(k.female_share)],
          ['Participação jovem', pct(k.youth_share)],
          ['Encaminhamentos pendentes', k.pending_action_items ?? 0]
        ].map(x => `<article><small>${x[0]}</small><strong>${x[1]}</strong></article>`).join('')}
      </section>
      <section class="panel">
        <h2>Participações por unidade</h2>
        <div class="chart" role="img" aria-label="Gráfico de participações por unidade">
          ${series.data.map((x: J) => `<div><span title="${h(x.unit_name)} (${h(x.municipality)})">${h(x.unit_name)}</span><i style="width:${Number(x.total) / max * 100}%"></i><b>${x.total}</b></div>`).join('') || '<p>Sem agregados no recorte selecionado.</p>'}
        </div>
      </section>
      <section class="panel table">
        <h2>Detalhamento territorial e agregados</h2>
        <div class="scroll">
          <table>
            <thead>
              <tr>
                <th>Grupo</th>
                <th>Unidade</th>
                <th>Município (IBGE)</th>
                <th>COREDE</th>
                <th>Região Funcional</th>
                <th>Bioma</th>
                <th>Total</th>
                <th>Mulheres</th>
                <th>Homens</th>
                <th>Jovens</th>
              </tr>
            </thead>
            <tbody>
              ${series.data.map((x: J) => `
                <tr>
                  <td>${h(x.group_code || '—')}</td>
                  <td><b>${h(x.unit_name)}</b></td>
                  <td>${h(x.municipality)} ${x.ibge_code ? `<small>(${x.ibge_code})</small>` : ''}</td>
                  <td>${h(x.corede || '—')}</td>
                  <td>${h(x.functional_region || '—')}</td>
                  <td><span class="badge tag">${h(x.biome_predominant || '—')}</span></td>
                  <td><b>${x.total}</b></td>
                  <td>${x.women ?? '—'}</td>
                  <td>${x.men ?? '—'}</td>
                  <td>${x.youth ?? '—'}</td>
                </tr>
              `).join('') || '<tr><td colspan="10">Nenhum dado encontrado no recorte.</tr>'}
            </tbody>
          </table>
        </div>
      </section>
      <section class="panel">
        <h2>Observações por validação</h2>
        ${(ov.observations_by_status ?? []).length ? (ov.observations_by_status ?? []).map((x: J) => `<span class="badge">${h(x.status)}: ${x.count}</span>`).join(' ') : '<p>Nenhuma observação no projeto.</p>'}
      </section>
    `;
  };

  (document.querySelector('#dash-project') as HTMLSelectElement).onchange = (e) => {
    const x = projectsRes.data.find((v: J) => v.id === (e.target as HTMLSelectElement).value);
    project = x.id;
    projectName = x.name;
    document.querySelector('#project-name')!.textContent = x.name;
    dashboard();
  };

  ['#group', '#unit', '#corede', '#rf', '#biome'].forEach(selector => {
    document.querySelector(selector)?.addEventListener('change', update);
  });

  document.querySelector('#reset')!.addEventListener('click', () => {
    ['#group', '#unit', '#corede', '#rf', '#biome'].forEach(selector => {
      const el = document.querySelector(selector) as HTMLSelectElement;
      if (el) el.value = '';
    });
    update();
  });

  await update();
}

async function units() {
  const [unitsRes, catalogRes, valuesRes] = await Promise.all([
    api(`/api/v1/units?project_id=${project}`),
    api('/api/v1/territories/catalog'),
    api(`/api/v1/analytics/units?project_id=${project}`)
  ]);

  const catalog: any[] = catalogRes.data || [];

  view('Territórios e Unidades', `
    <button id="new">Nova unidade</button>
    <section class="panel" id="form"></section>
    <section class="panel">
      <div class="map-heading"><div><h2>Mapa analítico territorial</h2><p class="muted">As cores representam valores agregados dos relatórios. Cinza significa ausência de dados.</p></div><div class="map-controls"><label>Camada<select id="map-layer"><option value="municipios">Municípios</option><option value="coredes">COREDEs</option><option value="regioes-funcionais">Regiões Funcionais</option><option value="biomas">Biomas</option></select></label><label>Indicador<select id="map-metric"><option value="total">Participações</option><option value="women">Mulheres</option><option value="men">Homens</option><option value="youth">Jovens</option></select></label></div></div>
      <div id="unit-map" aria-label="Mapa das unidades"></div>
      <div id="map-legend" class="map-legend"></div>
    </section>
    <section class="panel">
      <h2>Unidades cadastradas (${unitsRes.data.length})</h2>
      ${unitsRes.data.length ? `<div class="scroll"><table class="units-table"><thead><tr><th>Unidade</th><th>Município</th><th>COREDE / RF</th><th>Localização</th><th><span class="sr-only">Ações</span></th></tr></thead><tbody>${unitsRes.data.map((x: J) => `
        <tr>
          <td><b>${h(x.name)}</b><small>${h(x.code || '')}</small></td>
          <td>${h(x.municipality)}</td>
          <td>${h(x.corede || 'Pendente')}<small>${h(x.functional_region || 'RF pendente')}</small></td>
          <td>${x.latitude !== null && x.longitude !== null ? `${h(x.latitude)}, ${h(x.longitude)}` : '<span class="muted">Sem coordenadas</span>'}</td>
          <td class="row-actions"><button data-edit='${encodeURIComponent(JSON.stringify(x))}' class="secondary">Editar</button><button data-delete="${h(x.id)}" class="danger">Excluir</button></td>
        </tr>`).join('')}</tbody></table></div>` : '<p>Nenhuma unidade cadastrada neste projeto.</p>'}
    </section>
  `);

  const map = L.map('unit-map', { attributionControl: false, minZoom: 5, maxZoom: 12 }).setView([-30, -53], 6);
  map.getContainer().classList.add('offline-map');
  let thematic: any;
  const metricLabels: J = { total: 'Participações', women: 'Mulheres', men: 'Homens', youth: 'Jovens' };
  const renderMap = async () => {
    const layer = (document.querySelector('#map-layer') as HTMLSelectElement).value;
    const metric = (document.querySelector('#map-metric') as HTMLSelectElement).value;
    const geo = await api(`/api/v1/maps/${layer}`);
    const totals = new Map<string, number>();
    const keyForRow = (x: J) => layer === 'municipios' ? x.municipality : layer === 'coredes' ? x.corede : layer === 'regioes-funcionais' ? x.functional_region : x.biome_predominant;
    for (const x of valuesRes.data || []) { const key = keyForRow(x); if (key) totals.set(key, (totals.get(key) || 0) + Number(x[metric] || 0)); }
    const values = [...totals.values()].filter(x => x > 0); const max = Math.max(0, ...values);
    const color = (v: number | undefined) => v === undefined ? '#e4e8e5' : v === 0 ? '#d6e8df' : v <= max * .25 ? '#b8ddcc' : v <= max * .5 ? '#73b99c' : v <= max * .75 ? '#318267' : '#0e503e';
    if (thematic) map.removeLayer(thematic);
    const featureKey = (feature: any) => layer === 'biomas' ? feature.properties.id : feature.properties.nome;
    thematic = L.geoJSON(geo, { style: (feature: any) => { const value = totals.get(featureKey(feature)); return { color: '#557068', weight: 1, fillColor: color(value), fillOpacity: .82 }; }, onEachFeature: (feature: any, shape: any) => { const value = totals.get(featureKey(feature)); shape.bindPopup(`<b>${h(feature.properties.nome)}</b><br>${h(metricLabels[metric])}: <strong>${value === undefined ? 'sem dados' : value}</strong>`); } }).addTo(map);
    const bounds = thematic.getBounds(); if (bounds.isValid()) map.fitBounds(bounds, { padding: [15, 15] });
    document.querySelector('#map-legend')!.innerHTML = `<b>${h(metricLabels[metric])}</b><span><i style="background:#e4e8e5"></i>Sem dados</span><span><i style="background:#b8ddcc"></i>Baixo</span><span><i style="background:#73b99c"></i>Médio</span><span><i style="background:#0e503e"></i>Alto</span>`;
  };
  document.querySelector('#map-layer')!.addEventListener('change', renderMap);
  document.querySelector('#map-metric')!.addEventListener('change', renderMap);
  await renderMap(); setTimeout(() => map.invalidateSize(), 50);

  const render = (x: J = {}) => {
    document.querySelector('#form')!.innerHTML = `
      <h2>${x.id ? 'Editar' : 'Nova'} unidade</h2>
      <div class="provenance-box" id="prov-box" style="${x.ibge_code ? '' : 'display:none;'}">
        <b>Referência Territorial RS:</b> dados consultados no TRAMA-RS. Campos ainda não homologados ou desconhecidos permanecem vazios.
      </div>
      <form id="unit-form">
        <datalist id="muni-list">
          ${catalog.map((m: J) => `<option value="${h(m.municipality_name)}">${h(m.ibge_code || 'código pendente')} - ${h(m.corede_name)} (${h(m.rf_name)})</option>`).join('')}
        </datalist>
        <div class="form-grid">
          <label>Código da Unidade<input name="code" value="${h(x.code || '')}" required></label>
          <label>Nome da Unidade<input name="name" value="${h(x.name || '')}" required></label>
          <label>Município (497 do RS)
            <input name="municipality" id="muni-input" list="muni-list" value="${h(x.municipality || '')}" placeholder="Digite o nome ou código IBGE" required autocomplete="off">
          </label>
          <label>Código IBGE (7 dígitos)
            <input name="ibge_code" id="ibge-input" value="${h(x.ibge_code || '')}" readonly style="background:#f8f9fa;">
          </label>
          <label>COREDE (28 Conselhos)
            <input name="corede" id="corede-input" value="${h(x.corede || '')}" readonly style="background:#f8f9fa;">
          </label>
          <label>Região Funcional (9 RFs)
            <input name="functional_region" id="rf-input" value="${h(x.functional_region || '')}" readonly style="background:#f8f9fa;">
          </label>
          <label>Bioma Predominante
            <input name="biome_predominant" id="biome-input" value="${h(x.biome_predominant || x.biome || '')}" readonly style="background:#f8f9fa;">
          </label>
          <label>Biomas Ocorrentes (JSON / Lista)
            <input name="biomes_occurring" id="occ-input" value="${h(x.biomes_occurring || '')}" readonly style="background:#f8f9fa;">
          </label>
          <label>Latitude<input name="latitude" type="number" min="-90" max="90" step="any" value="${x.latitude ?? ''}"></label>
          <label>Longitude<input name="longitude" type="number" min="-180" max="180" step="any" value="${x.longitude ?? ''}"></label>
          <label>Tipo de Unidade<input name="unit_type" value="${h(x.unit_type || 'monitoring_unit')}"></label>
        </div>
        <button>Salvar Unidade</button>
      </form>
    `;

    const muniInput = document.querySelector('#muni-input') as HTMLInputElement;
    const updateTerritoryFromCatalog = async (query: string) => {
      if (!query) return;
      const found = catalog.find((c: J) => c.municipality_name.toLowerCase() === query.toLowerCase() || c.ibge_code === query);
      if (found) {
        (document.querySelector('#ibge-input') as HTMLInputElement).value = found.ibge_code || '';
        (document.querySelector('#corede-input') as HTMLInputElement).value = found.corede_name;
        (document.querySelector('#rf-input') as HTMLInputElement).value = found.rf_name;
        (document.querySelector('#biome-input') as HTMLInputElement).value = found.biome_predominant || '';
        (document.querySelector('#occ-input') as HTMLInputElement).value = found.biomes_occurring == null ? '' : (typeof found.biomes_occurring === 'string' ? found.biomes_occurring : JSON.stringify(found.biomes_occurring));
        document.querySelector('#prov-box')!.removeAttribute('style');
      } else {
        try {
          const res = await api(`/api/v1/territories/lookup?q=${encodeURIComponent(query)}`);
          if (res.data) {
            const d = res.data;
            (document.querySelector('#ibge-input') as HTMLInputElement).value = d.municipio.codigo_ibge || '';
            (document.querySelector('#corede-input') as HTMLInputElement).value = d.planejamento.corede.nome;
            (document.querySelector('#rf-input') as HTMLInputElement).value = d.planejamento.regiao_funcional.nome;
            (document.querySelector('#biome-input') as HTMLInputElement).value = d.ecologia.bioma_predominante || '';
            (document.querySelector('#occ-input') as HTMLInputElement).value = d.ecologia.biomas_ocorrentes == null ? '' : JSON.stringify(d.ecologia.biomas_ocorrentes);
            document.querySelector('#prov-box')!.removeAttribute('style');
          }
        } catch (_) {}
      }
    };

    muniInput.addEventListener('input', (e) => updateTerritoryFromCatalog((e.target as HTMLInputElement).value));
    muniInput.addEventListener('change', (e) => updateTerritoryFromCatalog((e.target as HTMLInputElement).value));

    (document.querySelector('#unit-form') as HTMLFormElement).onsubmit = async (e) => {
      e.preventDefault();
      const body = formData(e.currentTarget as HTMLFormElement);
      body.latitude = body.latitude === '' ? null : Number(body.latitude);
      body.longitude = body.longitude === '' ? null : Number(body.longitude);
      await api(x.id ? `/api/v1/units/${x.id}` : '/api/v1/units', {
        method: x.id ? 'PATCH' : 'POST',
        body: JSON.stringify({ ...body, project_id: project, revision: x.revision })
      });
      show('units');
    };
  };

  document.querySelector('#new')!.addEventListener('click', () => render());
  document.querySelectorAll('[data-edit]').forEach(b => b.addEventListener('click', () => render(JSON.parse(decodeURIComponent((b as HTMLElement).dataset.edit!)))));
  document.querySelectorAll('[data-delete]').forEach(b => b.addEventListener('click', () => remove(`/api/v1/units/${(b as HTMLElement).dataset.delete}`, 'units')));
}

async function records(kind: string) {
  const path = kind === 'action-items' ? 'action-items' : kind;
  const j = await api(`/api/v1/${path}?project_id=${project}`);
  const labels: any = { activities: 'Atividades', observations: 'Observações', 'action-items': 'Encaminhamentos' };
  
  view(labels[kind], `
    <button id="new">Novo registro</button>
    <section class="panel" id="form"></section>
    <section class="panel">
      ${j.data.length ? j.data.map((x: J) => `
        <article class="record">
          <b>${h(x.title || x.topic)}</b>
          <span>${h(x.status || x.validation_status)} · revisão ${x.revision}</span>
          <p>${h(x.description || x.statement || x.details || '')}</p>
          <button data-edit='${encodeURIComponent(JSON.stringify(x))}' class="secondary">Editar</button>
          <button data-delete="${h(x.id)}" class="danger">Excluir</button>
        </article>
      `).join('') : '<p>Nenhum registro.</p>'}
    </section>
  `);

  const activities = kind === 'observations' ? (await api(`/api/v1/activities?project_id=${project}`)).data : [];
  const render = (x: J = {}) => {
    let fields = kind === 'activities' ? `
      <label>Título<input name="title" value="${h(x.title || '')}" required></label>
      <label>Descrição<textarea name="description">${h(x.description || '')}</textarea></label>
      <label>Status<input name="status" value="${h(x.status || 'planned')}"></label>
    ` : kind === 'observations' ? `
      <label>Atividade<select name="activity_id" ${x.id ? 'disabled' : ''}>${activities.map((a: J) => `<option value="${h(a.id)}" ${a.id === x.activity_id ? 'selected' : ''}>${h(a.title)}</option>`).join('')}</select></label>
      <label>Tema<input name="topic" value="${h(x.topic || '')}" required></label>
      <label>Declaração<textarea name="statement" required>${h(x.statement || '')}</textarea></label>
      <label>Validação<select name="validation_status"><option ${x.validation_status === 'draft' ? 'selected' : ''}>draft</option><option ${x.validation_status === 'review_required' ? 'selected' : ''}>review_required</option><option ${x.validation_status === 'validated' ? 'selected' : ''}>validated</option><option ${x.validation_status === 'rejected' ? 'selected' : ''}>rejected</option></select></label>
    ` : `
      <label>Título<input name="title" value="${h(x.title || '')}" required></label>
      <label>Detalhes<textarea name="details">${h(x.details || '')}</textarea></label>
      <label>Estado<select name="status"><option ${x.status === 'open' ? 'selected' : ''}>open</option><option ${x.status === 'in_progress' ? 'selected' : ''}>in_progress</option><option ${x.status === 'completed' ? 'selected' : ''}>completed</option><option ${x.status === 'blocked' ? 'selected' : ''}>blocked</option><option ${x.status === 'cancelled' ? 'selected' : ''}>cancelled</option></select></label>
      <label>Prioridade<input name="priority" value="${h(x.priority || 'normal')}"></label>
    `;

    document.querySelector('#form')!.innerHTML = `<h2>${x.id ? 'Editar' : 'Novo'} registro</h2><form id="record-form">${fields}<button>Salvar</button></form>`;
    (document.querySelector('#record-form') as HTMLFormElement).onsubmit = async (e) => {
      e.preventDefault();
      let body: J = { ...formData(e.currentTarget as HTMLFormElement), project_id: project, revision: x.revision };
      if (kind === 'observations') {
        body.observation_kind = x.observation_kind || 'reported';
        body.epistemic_status = x.epistemic_status || 'observed';
      }
      await api(x.id ? `/api/v1/${path}/${x.id}` : `/api/v1/${path}`, {
        method: x.id ? 'PATCH' : 'POST',
        body: JSON.stringify(body)
      });
      show(kind === 'action-items' ? 'actions' : kind);
    };
  };

  document.querySelector('#new')!.addEventListener('click', () => render());
  document.querySelectorAll('[data-edit]').forEach(b => b.addEventListener('click', () => render(JSON.parse(decodeURIComponent((b as HTMLElement).dataset.edit!)))));
  document.querySelectorAll('[data-delete]').forEach(b => b.addEventListener('click', () => remove(`/api/v1/${path}/${(b as HTMLElement).dataset.delete}`, kind === 'action-items' ? 'actions' : kind)));
}

async function reports() {
  if (!project) {
    view('Relatórios', `<div class="notice">Selecione ou crie um projeto para gerar relatórios.</div>`);
    return;
  }

  const [projectsRes, groupsRes, unitsRes, dimsRes] = await Promise.all([
    api('/api/v1/projects'),
    api(`/api/v1/project-groups?project_id=${project}`),
    api(`/api/v1/units?project_id=${project}`),
    api(`/api/v1/analytics/dimensions?project_id=${project}`)
  ]);

  const coredes: any[] = dimsRes.data?.coredes || [];
  const rfs: any[] = dimsRes.data?.functional_regions || [];
  const biomes: any[] = dimsRes.data?.biomes || [];

  view('Relatórios Técnicos', `
    <section class="filters report-filters" aria-label="Filtros do relatório">
      <label>Projeto
        <select id="report-project">${projectsRes.data.map((x: J) => `<option value="${h(x.id)}" ${x.id === project ? 'selected' : ''}>${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Grupo
        <select id="report-group"><option value="">Todos os Grupos</option>${groupsRes.data.map((x: J) => `<option value="${h(x.id)}">${h(x.label)}</option>`).join('')}</select>
      </label>
      <label>Unidade (UAC)
        <select id="report-unit"><option value="">Todas as Unidades</option>${unitsRes.data.map((x: J) => `<option value="${h(x.id)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>COREDE
        <select id="report-corede"><option value="">Todos os 28 COREDEs</option>${coredes.map((x: J) => `<option value="${h(x.name)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Região Funcional
        <select id="report-rf"><option value="">Todas as 9 RFs</option>${rfs.map((x: J) => `<option value="${h(x.name)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Bioma
        <select id="report-biome"><option value="">Todos os Biomas</option>${biomes.map((x: J) => `<option value="${h(x.name)}">${h(x.name)}</option>`).join('')}</select>
      </label>
      <label>Incluir Mapa
        <select id="report-inc-map"><option value="1">Sim (com Mapa)</option><option value="0">Não (apenas Tabelas)</option></select>
      </label>
      <label>Camada do Mapa
        <select id="report-map-layer"><option value="municipios">Municípios</option><option value="coredes">COREDEs</option><option value="regioes-funcionais">Regiões Funcionais</option><option value="biomas">Biomas</option></select>
      </label>
      <label>Indicador do Mapa
        <select id="report-map-metric"><option value="total">Participações</option><option value="women">Mulheres</option><option value="men">Homens</option><option value="youth">Jovens</option></select>
      </label>
      <button id="report-reset" class="secondary">Limpar filtros</button>
    </section>

    <section class="panel">
      <h2>Emissão de Relatório Técnico Completo</h2>
      <p>Relatório executivo e territorial com consolidação analítica, distribuição espacial no mapa e proveniência metodológica para o projeto <strong>${h(projectName)}</strong>.</p>
      <div class="actions" style="display:flex;gap:0.75rem;flex-wrap:wrap;margin:1.25rem 0;">
        <button id="report-update" class="button">🔄 Atualizar Pré-visualização</button>
        <a id="report-btn-tab" class="button secondary" href="#" target="_blank" rel="noopener">Abrir em Nova Aba / Salvar PDF</a>
        <a id="report-btn-csv" class="button secondary" href="#">Exportar CSV</a>
        <a id="report-btn-json" class="button secondary" href="#">Exportar JSON</a>
      </div>
      <div id="report-status" class="meta" style="margin-top:0.5rem;font-weight:600;"></div>
      <div id="report-frame-container" style="margin-top:1.5rem;">
        <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:0.75rem;">
          <strong>Pré-visualização do Relatório Oficial</strong>
          <button id="print-inline-btn" class="button secondary" style="padding:0.35rem 0.75rem;font-size:0.85rem;">🖨️ Imprimir / Salvar PDF</button>
        </div>
        <iframe id="report-frame" style="width:100%;min-height:750px;border:1px solid #cbd5e1;border-radius:8px;background:#fff;" title="Prévia do Relatório"></iframe>
      </div>
    </section>
  `);

  const statusEl = document.querySelector('#report-status')!;
  const frameContainer = document.querySelector('#report-frame-container') as HTMLDivElement;
  const frame = document.querySelector('#report-frame') as HTMLIFrameElement;
  const printBtn = document.querySelector('#print-inline-btn') as HTMLButtonElement;

  const getParams = () => {
    const g = (document.querySelector('#report-group') as HTMLSelectElement)?.value || '';
    const u = (document.querySelector('#report-unit') as HTMLSelectElement)?.value || '';
    const c = (document.querySelector('#report-corede') as HTMLSelectElement)?.value || '';
    const rf = (document.querySelector('#report-rf') as HTMLSelectElement)?.value || '';
    const b = (document.querySelector('#report-biome') as HTMLSelectElement)?.value || '';
    const incMap = (document.querySelector('#report-inc-map') as HTMLSelectElement)?.value || '1';
    const mapLayer = (document.querySelector('#report-map-layer') as HTMLSelectElement)?.value || 'municipios';
    const mapMetric = (document.querySelector('#report-map-metric') as HTMLSelectElement)?.value || 'total';

    const q = new URLSearchParams({ project_id: project });
    if (g) q.set('group_id', g);
    if (u) q.set('unit_id', u);
    if (c) q.set('corede', c);
    if (rf) q.set('functional_region', rf);
    if (b) q.set('biome', b);
    q.set('include_map', incMap);
    q.set('map_layer', mapLayer);
    q.set('map_metric', mapMetric);
    return q;
  };

  const updateUrlsAndPreview = async () => {
    const q = getParams();
    const qs = q.toString();
    const tabLink = document.querySelector('#report-btn-tab') as HTMLAnchorElement;
    const csvLink = document.querySelector('#report-btn-csv') as HTMLAnchorElement;
    const jsonLink = document.querySelector('#report-btn-json') as HTMLAnchorElement;

    if (tabLink) tabLink.href = `/api/v1/reports/preview?${qs}`;
    if (csvLink) csvLink.href = `/api/v1/exports/attendance.csv?${qs}`;
    if (jsonLink) jsonLink.href = `/api/v1/exports/attendance.json?${qs}`;

    try {
      statusEl.textContent = 'Carregando dados do relatório...';
      const html = await api(`/api/v1/reports/preview?${qs}`);
      frameContainer.style.display = 'block';
      frame.srcdoc = typeof html === 'string' ? html : JSON.stringify(html);
      statusEl.textContent = 'Relatório gerado com sucesso.';
    } catch (err: any) {
      statusEl.textContent = 'Erro ao gerar relatório: ' + (err.message || err);
    }
  };

  (document.querySelector('#report-project') as HTMLSelectElement).onchange = (e) => {
    const x = projectsRes.data.find((v: J) => v.id === (e.target as HTMLSelectElement).value);
    project = x.id;
    projectName = x.name;
    document.querySelector('#project-name')!.textContent = x.name;
    reports();
  };

  ['#report-group', '#report-unit', '#report-corede', '#report-rf', '#report-biome', '#report-inc-map', '#report-map-layer', '#report-map-metric'].forEach(sel => {
    document.querySelector(sel)?.addEventListener('change', updateUrlsAndPreview);
  });

  document.querySelector('#report-update')!.addEventListener('click', updateUrlsAndPreview);

  document.querySelector('#report-reset')!.addEventListener('click', () => {
    ['#report-group', '#report-unit', '#report-corede', '#report-rf', '#report-biome'].forEach(sel => {
      const el = document.querySelector(sel) as HTMLSelectElement;
      if (el) el.value = '';
    });
    const mapInc = document.querySelector('#report-inc-map') as HTMLSelectElement;
    if (mapInc) mapInc.value = '1';
    updateUrlsAndPreview();
  });

  printBtn?.addEventListener('click', () => {
    if (frame.contentWindow) {
      frame.contentWindow.focus();
      frame.contentWindow.print();
    }
  });

  await updateUrlsAndPreview();
}

async function sources() {
  const j = await api('/api/v1/sources');
  view('Fontes e Qualidade', `
    <section class="panel">
      <h2>Fontes Documentais</h2>
      ${j.data.map((x: J) => `
        <article class="record">
          <b>${h(x.title)}</b>
          <span>Tipo: ${h(x.kind)} · Status: ${h(x.provenance_status)} · SHA256: ${h(x.sha256 || '—')}</span>
        </article>
      `).join('') || '<p>Nenhuma fonte vinculada.</p>'}
    </section>
  `);
}

api('/api/v1/auth/me').then((j: J) => layout(j.user)).catch(() => login());
