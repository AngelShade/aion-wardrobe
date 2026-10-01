(function () {
  'use strict';
  var state = null, query = { category: 'All', filter: 'unlocked', q: '', page: 1, target: 0, skin: 0 }, view = 'collection', equipmentTab = 'equipped', request = null, sequence = 0, busy = false, debounce = null, confirmation = null, priorFocus = null;
  var session = '', parts = window.location.search.substring(1).split('&');
  var skinIndex = {}, drafts = {}, browseCache = {}, pendingNative = null, nativeStarted = false, nativeRequest = 0, nativePoll = null;
  for (var z = 0; z < parts.length; z++) { var pair = parts[z].split('='); if (pair[0] === 'session_id') session = decodeURIComponent(pair[1] || ''); }
  function id(name) { return document.getElementById(name); }
  // Aion's WebKit does not implement HTMLElement.hidden. Toggle the attribute
  // itself so confirmations and tabs follow the same [hidden] CSS rule.
  function hide(node, value) { if (value) node.setAttribute('hidden', ''); else node.removeAttribute('hidden'); }
  function hidden(node) { return node.hasAttribute('hidden'); }
  function esc(value) { return String(value == null ? '' : value).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;'); }
  function form(values) { var out = [], key; for (key in values) if (values.hasOwnProperty(key)) out.push(encodeURIComponent(key) + '=' + encodeURIComponent(values[key])); return out.join('&'); }
  function params(extra) { var p = { session_id: session }, key; for (key in query) if (query.hasOwnProperty(key)) p[key] = query[key]; for (key in extra) if (extra.hasOwnProperty(key)) p[key] = extra[key]; return p; }
  function status(text, error) { id('status').textContent = text; id('status').style.color = error ? '#f2a38a' : ''; id('connection').className = error ? '' : 'online'; }
  function icon(item, tooltip, size) {
    var url = 'nc://aion.ItemInfo/ItemTooltip?' + (tooltip || 'item=' + item + '&count=1&enchant_count=0&authorize_count=0');
    return '<a class="item-icon" href="' + esc(url) + '" title="' + esc(url) + '"' + (size ? ' style="width:' + size + 'px;height:' + size + 'px"' : '') + '><img src="/market/media/icons/' + (+item) + '.png?v=wardrobe1" alt=""></a>';
  }
  function bindIcons(root) { var anchors = root.querySelectorAll('.item-icon'); for (var i = 0; i < anchors.length; i++) anchors[i].onclick = function (event) { if (event) event.preventDefault(); return false; }; }
  function cacheKey() { return [query.category, query.filter, query.q, query.page, query.target].join('|'); }
  function receive(result, action) {
    state = result; query.page = result.page; query.target = result.target;
    if (action) skinIndex = {};
    for (var a = 0; a < (result.outfitAppearances || []).length; a++) skinIndex[result.outfitAppearances[a].item] = result.outfitAppearances[a];
    for (var i = 0; i < result.skins.length; i++) skinIndex[result.skins[i].item] = result.skins[i];
    if (result.selected) skinIndex[result.selected.item] = result.selected;
    if (action) {
      browseCache = {}; nativeStarted = false;
      if (action === 'applyChanges' || action === 'applyOutfit' || action === 'restore' || action === 'apply') { drafts = {}; previewWing = null; }
      else for (var object in drafts) if (drafts.hasOwnProperty(object) && skinIndex[drafts[object].skin]) drafts[object].unlocked = skinIndex[drafts[object].skin].unlocked;
      closeDialog();
    }
    render(); status(result.notice || 'Wardrobe ready.', false);
    if (!nativeStarted) { nativeStarted = true; updatePreview(); }
  }
  function fetchState(extra, action) {
    if (busy) return;
    if (request) request.abort();
    for (var cache in browseCache) if (browseCache.hasOwnProperty(cache) && browseCache[cache].expires <= Date.now()) delete browseCache[cache];
    var cached = browseCache[cacheKey()];
    if (!action && !extra && cached && cached.expires > Date.now()) { ++sequence; request = null; receive(cached.state, false); return; }
    var ticket = ++sequence, xhr = new XMLHttpRequest(), data = params(extra || {});
    if (action) { busy = true; data.request = state.request; id('confirm').disabled = true; }
    request = xhr; status(action ? 'Updating Wardrobe…' : 'Loading appearances…', false);
    xhr.open(action ? 'POST' : 'GET', '/market/wardrobe/' + (action ? 'action' : 'state') + (action ? '' : '?' + form(data)), true);
    if (action) xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.onreadystatechange = function () {
      if (xhr.readyState !== 4 || ticket !== sequence) return;
      request = null; busy = false; id('confirm').disabled = false;
      var result; try { result = JSON.parse(xhr.responseText); } catch (e) { result = { error: 'Wardrobe could not load. Refresh before trying again.' }; }
      if (xhr.status !== 200 || result.error) { var message = result.error || 'Wardrobe could not load. Refresh before trying again.'; status(message, true); if (!hidden(id('shade'))) id('dialog-error').textContent = message; return; }
      if (!action) browseCache[cacheKey()] = { state: result, expires: Date.now() + 2500 };
      receive(result, action ? data.action : false);
    };
    xhr.send(action ? form(data) : null);
  }
  function target() { if (!state) return null; for (var i = 0; i < state.equipment.length; i++) if (+state.equipment[i].object === +query.target) return state.equipment[i]; return null; }
  function renderEquipment() {
    var term = id('equipment-search').value.toLowerCase(), html = '', total = 0;
    for (var i = 0; i < state.equipment.length; i++) { var item = state.equipment[i];
      if (!!item.equipped !== (equipmentTab === 'equipped') || item.name.toLowerCase().indexOf(term) < 0) continue;
      var draft = drafts[item.object];
      total++; html += '<button class="equipment-item' + (+item.object === +query.target ? ' selected' : '') + (draft ? ' drafted' : '') + '" data-object="' + item.object + '">' + icon(draft ? draft.skin || item.item : item.skin, item.tooltip) + '<span class="item-name">' + esc(item.name) + '</span><small>' + esc(item.type) + '</small>' + (draft ? '<span class="draft-label">' + esc(draft.name) + '</span>' : '') + '</button>';
    }
    id('equipment-count').textContent = total + ' items';
    html = html || '<div class="empty-collection"><p>No compatible equipment.</p></div>';
    if (id('equipment-list')._content !== html) { id('equipment-list').innerHTML = html; id('equipment-list')._content = html; }
    var buttons = id('equipment-list').querySelectorAll('[data-object]');
    for (i = 0; i < buttons.length; i++) buttons[i].onclick = function () { query.target = +this.getAttribute('data-object'); query.page = 1; fetchState(); };
    bindIcons(id('equipment-list'));
    var selected = target(); id('target-name').textContent = selected ? selected.name : 'Select equipment'; id('restore').disabled = !selected || +selected.skin === +selected.item;
  }
  function renderCollection() {
    var html = '', i, skin;
    for (i = 0; i < state.categories.length; i++) html += '<button data-category="' + esc(state.categories[i]) + '" class="' + (query.category === state.categories[i] ? 'active' : '') + '">' + esc(state.categories[i]) + '</button>';
    if (id('categories')._content !== html) { id('categories').innerHTML = html; id('categories')._content = html; }
    var cats = id('categories').querySelectorAll('button'); for (i = 0; i < cats.length; i++) cats[i].onclick = function () { query.category = this.getAttribute('data-category'); query.target = 0; query.page = 1; fetchState(); };
    html = '';
    for (i = 0; i < state.skins.length; i++) { skin = state.skins[i]; var owned = skin.sources.length > 0;
      html += '<div class="skin-card ' + esc(skin.quality) + (+skin.item === +query.skin ? ' selected' : '') + '" data-skin="' + skin.item + '" tabindex="0" role="button" aria-label="' + esc(skin.name) + '">' + icon(skin.item) + '<span class="skin-name">' + esc(skin.name) + '</span><span class="skin-state ' + (skin.unlocked ? '' : owned ? 'owned' : 'locked') + '">' + (skin.unlocked ? 'Unlocked' : owned ? 'Ready to unlock' : 'Locked') + '</span></div>';
    }
    if (!html) html = '<div class="empty-collection"><div class="wardrobe-mark">◇</div><h2>' + (query.filter === 'unlocked' ? 'No unlocked appearances' : 'No matching appearances') + '</h2><p>' + (query.filter === 'unlocked' ? 'Keep an appearance item in Inventory and use one Appearance Unlock to add its skin to your account.' : 'Change your search, equipment selection or appearance filter.') + '</p><button id="browse-owned">Ready to Unlock</button><button id="browse-all">All Appearances</button></div>';
    if (id('skin-grid')._content !== html) { id('skin-grid').innerHTML = html; id('skin-grid')._content = html; }
    var cards = id('skin-grid').querySelectorAll('[data-skin]');
    for (i = 0; i < cards.length; i++) { cards[i].onclick = function () {
      query.skin = +this.getAttribute('data-skin');
      var siblings = id('skin-grid').querySelectorAll('[data-skin]');
      for (var k = 0; k < siblings.length; k++) { siblings[k].className = siblings[k].className.replace(/ selected/g, ''); if (+siblings[k].getAttribute('data-skin') === query.skin) siblings[k].className += ' selected'; }
      id('skin-grid')._content = null; renderDetail();
    }; cards[i].onkeydown = function (e) { if (e.keyCode === 13 || e.keyCode === 32) { e.preventDefault(); this.onclick(); } }; }
    if (id('browse-owned')) id('browse-owned').onclick = function () { query.filter = 'owned'; id('filter').value = 'owned'; query.page = 1; fetchState(); };
    if (id('browse-all')) id('browse-all').onclick = function () { query.filter = 'all'; id('filter').value = 'all'; query.page = 1; fetchState(); };
    bindIcons(id('skin-grid')); id('results').textContent = state.results + ' appearances' + (target() ? ' · Compatible with selected equipment' : '');
    hide(id('clear-target'), !query.target); id('page').textContent = state.page + ' / ' + state.pages; id('previous').disabled = state.page <= 1; id('next').disabled = state.page >= state.pages;
  }
  function compatible(item, skin) {
    if (!item || !skin) return false;
    if (!item.group || !skin.group) return !!skin.compatible;
    var weapons = /^(SWORD|GREATSWORD|DAGGER|MACE|ORB|SPELLBOOK|POLEARM|STAFF|BOW|HARP|GUN|CANNON|KEYBLADE)$/;
    if (weapons.test(item.group) || weapons.test(skin.group)) return weapons.test(item.group) && weapons.test(skin.group) && item.group === skin.group;
    return (item.slots & skin.slots) !== 0;
  }
  var previewWing = null;
  function updatePreview() {
    clearTimeout(pendingNative);
    pendingNative = setTimeout(function () {
      if (!state) return;
      var items = [], i, item, draft, count = 0, lockedNames = [], wingDraft = !!previewWing;
      for (i = 0; i < state.equipment.length; i++) { item = state.equipment[i]; draft = drafts[item.object]; if (item.equipped) items.push(draft ? draft.skin || item.item : item.skin); if (draft) { count++; if (!draft.unlocked) lockedNames.push(draft.name); if (item.group === 'WING') wingDraft = true; } }
      for (i = 0; i < state.equipment.length; i++) { item = state.equipment[i]; draft = drafts[item.object]; if (!item.equipped && draft) items.push(draft.skin || item.item); }
      if (previewWing) items.push(+previewWing.item);
      id('draft-count').textContent = count ? count + ' appearance changes' : 'Current equipment';
      id('apply-changes').disabled = !count || lockedNames.length > 0;
      id('apply-changes').title = lockedNames.length ? 'Unlock or remove: ' + lockedNames.join(', ') : '';
      hide(id('remove-locked-preview'), !lockedNames.length);
      id('remove-locked-preview').title = lockedNames.join(', ');
      id('draft-count').title = lockedNames.length ? 'Locked appearances: ' + lockedNames.join(', ') : '';
      if (lockedNames.length) {
        id('draft-count').textContent += ' · ' + lockedNames.length + ' locked';
        status('Unlock or remove these preview appearances: ' + lockedNames.join(', '), true);
      }
      if (!window.AionObject) { id('preview-message').textContent = 'Character preview is available in Aion.'; return; }
      // The native bridge resets the existing paper-doll and adds every appearance in one UI pass.
      try {
        if (window.AionObject.WardrobePreview && window.AionObject.WardrobePoll) {
          var r = id('native-preview').getBoundingClientRect(), token = ++nativeRequest, attempts = 0;
          clearTimeout(nativePoll); id('preview-message').textContent = 'Loading character preview…';
          window.AionObject.WardrobePreview(items.join(','), Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height), token);
          if (wingDraft) nativeControl('wings',1);
          function poll() {
            if (nativeRequest !== token) return;
            if (++attempts > 40) { window.WardrobePreviewState(false, token); return; }
            nativePoll = setTimeout(poll, 150);
            window.AionObject.WardrobePoll();
          }
          nativePoll = setTimeout(poll, 150);
        }
        else { id('preview-message').textContent = 'Install the Wardrobe client update to use the character preview.'; return; }
      } catch (e) { status('Character preview could not update.', true); }
    }, 80);
  }
  window.WardrobePreviewState = function (ready, token) {
    if (token !== nativeRequest) return;
    clearTimeout(nativePoll); id('preview-message').textContent = ready ? '' : 'Character preview could not load. Close and reopen Wardrobe to try again.';
  };
  window.onunload = function () { clearTimeout(nativePoll); clearTimeout(pendingNative); };
  function tryOn(skin) {
    var selected = target(), i;
    if (!compatible(selected, skin)) for (i = 0; i < state.equipment.length; i++) if (state.equipment[i].equipped && compatible(state.equipment[i], skin)) { selected = state.equipment[i]; break; }
    if (!compatible(selected, skin)) {
      if (skin.group === 'WING') { previewWing = skin; updatePreview(); status('Wing preview ready. Equip wings to apply this appearance.', false); return; }
      status('Select compatible equipment before trying on this appearance.', true); return;
    }
    if (skin.group === 'WING') previewWing = null;
    if (+skin.item === +selected.skin) delete drafts[selected.object];
    else drafts[selected.object] = { object: selected.object, skin: +skin.item, expected: +selected.skin, name: skin.name, unlocked: skin.unlocked };
    renderEquipment(); updatePreview(); status('Appearance added to preview.', false);
  }
  function renderDetail() {
    var skin = skinIndex[query.skin], selected = target(), html = '';
    if (!skin) { id('detail').innerHTML = '<div class="empty-detail"><h2>Select an appearance</h2><p>Select equipment, then try on a compatible appearance.</p></div>'; return; }
    html = '<div class="detail-art">' + icon(skin.item) + '</div><h2 class="detail-name">' + esc(skin.name) + '</h2><div class="detail-category">' + esc(skin.category + ' · ' + skin.type) + '</div><div class="unlock-badge' + (skin.unlocked ? '' : ' locked') + '">' + (skin.unlocked ? 'Unlocked' : 'Locked appearance') + '</div><div class="detail-actions"><button id="preview">Try On</button>';
    if (skin.unlocked) html += '<span class="detail-description">Reusable skin.</span>';
    else html += '<button id="unlock" class="primary"' + (!skin.sources.length || !state.tickets ? ' disabled' : '') + '>Unlock Appearance</button><p class="detail-description">Uses one Appearance Unlock. Keeps your equipment. Unlocks this skin for every character on your account.</p>';
    html += '</div><div class="target-summary">Selected equipment<strong>' + esc(selected ? selected.name : 'Select equipment on the right') + '</strong></div>';
    if (!skin.unlocked) html += '<div class="unlock-help">' + (skin.sources.length ? 'Appearance item: <strong>' + esc(skin.sources[0].name) + '</strong><br>' : 'Obtain this appearance item and keep it in Inventory.<br>') + (state.tickets ? state.tickets + ' Appearance Unlock available.' : 'Appearance Unlock is available in Cash Shop → Character services.') + '</div>';
    id('detail').innerHTML = html; bindIcons(id('detail')); id('preview').onclick = function () { tryOn(skin); };
    if (id('unlock')) id('unlock').onclick = function () { showDialog('Unlock Appearance', '<p>Unlock <strong>' + esc(skin.name) + '</strong> for your account?</p><p>Consumes <strong>1 Appearance Unlock</strong>. Your equipment is kept.</p>', function () { fetchState({ action: 'unlock', skin: skin.item, source: skin.sources[0].object }, true); }); };
  }
  function renderOutfits() {
    var html = '', i, j, outfit, skins, count;
    for (i = 0; i < state.outfits.length; i++) { outfit = state.outfits[i]; skins = JSON.parse(outfit.skins_json); count = 0;
      html += '<div class="outfit-card"><h3>' + esc(outfit.name) + '</h3><div class="outfit-icons">';
      for (j in skins) if (skins.hasOwnProperty(j)) { count++; if (+skins[j]) html += icon(skins[j]); }
      html += '</div><small>' + count + ' equipment slots · Original appearances are saved too.</small><button class="primary" data-outfit="' + esc(outfit.name) + '">Try On Outfit</button><button data-delete="' + esc(outfit.name) + '">Delete</button></div>';
    }
    id('outfit-list').innerHTML = html || '<div class="empty-collection"><h2>No saved outfits</h2><p>Apply unlocked skins to equipped items, then save your current outfit.</p></div>';
    var buttons = id('outfit-list').querySelectorAll('[data-outfit]'); for (i = 0; i < buttons.length; i++) buttons[i].onclick = function () {
      var name = this.getAttribute('data-outfit'), saved = null, staged = {}, key, j;
      for (j = 0; j < state.outfits.length; j++) if (state.outfits[j].name === name) saved = JSON.parse(state.outfits[j].skins_json);
      if (!saved) return;
      for (key in saved) if (saved.hasOwnProperty(key)) {
        var item = null, appearance = +saved[key];
        for (j = 0; j < state.equipment.length; j++) if (state.equipment[j].equipped && +state.equipment[j].slot === +key) item = state.equipment[j];
        if (!item || appearance && !compatible(item,skinIndex[appearance])) { status('Equip compatible items for every saved outfit slot.',true); return; }
        if ((appearance || item.item) !== +item.skin) staged[item.object] = { object:item.object,skin:appearance,expected:+item.skin,name:appearance ? skinIndex[appearance].name : item.name + ' · Original appearance',unlocked:!appearance || skinIndex[appearance].unlocked };
      }
      drafts = staged; previewWing = null; renderEquipment(); updatePreview(); status('Outfit added to preview.',false);
    };
    buttons = id('outfit-list').querySelectorAll('[data-delete]'); for (i = 0; i < buttons.length; i++) buttons[i].onclick = function () { var name = this.getAttribute('data-delete'); showDialog('Delete Outfit', '<p>Delete <strong>' + esc(name) + '</strong>?</p><p>Your unlocked appearances are kept.</p>', function () { fetchState({ action: 'deleteOutfit', name: name }, true); }); };
    bindIcons(id('outfit-list'));
  }
  function render() { id('character').textContent = state.character + ' · Account appearances'; id('collection-count').textContent = state.unlocked + ' / ' + state.total; id('tickets').textContent = state.tickets; renderEquipment(); renderCollection(); renderDetail(); if (view === 'outfits') renderOutfits(); }
  function setView(next) { view = next; hide(id('collection-view'), next !== 'collection'); hide(id('outfits-view'), next !== 'outfits'); id('collection-tab').className = next === 'collection' ? 'active' : ''; id('outfits-tab').className = next === 'outfits' ? 'active' : ''; }
  function nativeControl(command, pressed) { if (window.AionObject && window.AionObject.WardrobeControl) window.AionObject.WardrobeControl(command, pressed || 0); }
  function showDialog(title, body, action) { if (busy) return; priorFocus = document.activeElement; confirmation = action; id('dialog-title').textContent = title; id('dialog-body').innerHTML = body; id('dialog-error').textContent = ''; hide(id('shade'), false); nativeControl('visible',0); id('confirm').disabled = false; var input = id('dialog-body').querySelector('input'); (input || id('cancel')).focus(); }
  function closeDialog() { if (busy) return; hide(id('shade'), true); nativeControl('visible',1); confirmation = null; if (priorFocus && document.body.contains(priorFocus)) priorFocus.focus(); }
  id('confirm').onclick = function () { if (confirmation && !busy) confirmation(); };
  id('cancel').onclick = id('dialog-close').onclick = closeDialog;
  id('refresh').onclick = function () { browseCache = {}; skinIndex = {}; fetchState({ refresh: 1 }); };
  id('collection-tab').onclick = function () { setView('collection'); };
  id('outfits-tab').onclick = function () { setView('outfits'); if (state) renderOutfits(); };
  id('reset-preview').onclick = function () { nativeControl('camera-reset',1); nativeControl('wings',0); previewWing = null; drafts = {}; renderEquipment(); updatePreview(); };
  id('remove-locked-preview').onclick = function () {
    for (var object in drafts) if (drafts.hasOwnProperty(object) && !drafts[object].unlocked) delete drafts[object];
    renderEquipment(); updatePreview(); status('Locked appearances removed from preview.', false);
  };
  id('apply-changes').onclick = function () {
    var changes = [], list = '', key;
    for (key in drafts) if (drafts.hasOwnProperty(key)) { var d = drafts[key]; if (!d.unlocked) { status('Unlock selected appearances before applying changes.', true); return; } changes.push({ object: d.object, skin: d.skin, expected: d.expected }); list += '<li>' + esc(d.name) + '</li>'; }
    if (changes.length) showDialog('Apply Changes', '<p>Apply these appearances?</p><ul>' + list + '</ul><p>Equipment attributes and dye are kept.</p>', function () { fetchState({ action: 'applyChanges', changes: JSON.stringify(changes) }, true); });
  };
  id('equipped-tab').onclick = function () { equipmentTab = 'equipped'; this.className = 'active'; id('inventory-tab').className = ''; if (state) renderEquipment(); };
  id('inventory-tab').onclick = function () { equipmentTab = 'inventory'; this.className = 'active'; id('equipped-tab').className = ''; if (state) renderEquipment(); };
  id('equipment-search').oninput = function () { if (state) renderEquipment(); };
  id('search').oninput = function () { clearTimeout(debounce); debounce = setTimeout(function () { query.q = id('search').value; query.page = 1; fetchState(); }, 220); };
  id('search').onkeydown = function (e) { if (e.keyCode === 13) { clearTimeout(debounce); query.q = this.value; query.page = 1; fetchState(); } };
  id('clear-search').onclick = function () { id('search').value = ''; query.q = ''; query.page = 1; fetchState(); id('search').focus(); };
  id('filter').onchange = function () { query.filter = this.value; query.page = 1; fetchState(); };
  id('clear-target').onclick = function () { query.target = 0; query.page = 1; fetchState(); };
  id('previous').onclick = function () { if (query.page > 1) { query.page--; fetchState(); } };
  id('next').onclick = function () { if (state && query.page < state.pages) { query.page++; fetchState(); } };
  id('restore').onclick = function () { var selected = target(); if (selected) { drafts[selected.object] = { object:selected.object,skin:0,expected:+selected.skin,name:selected.name + ' · Original appearance',unlocked:true }; renderEquipment(); updatePreview(); } };
  id('save-outfit').onclick = function () { showDialog('Save Current Outfit', '<label for="outfit-name">Outfit name</label><input id="outfit-name" maxlength="32" placeholder="Outfit name"><p>Uses your equipped appearances. All remodeled skins must be unlocked.</p>', function () { var name = id('outfit-name').value.replace(/^\s+|\s+$/g, ''); if (!name) { id('dialog-error').textContent = 'Enter an outfit name.'; return; } var exists = false; for (var i = 0; i < state.outfits.length; i++) if (state.outfits[i].name.toLowerCase() === name.toLowerCase()) exists = true; if (exists && !id('overwrite-outfit')) { id('dialog-error').innerHTML = 'An outfit with this name exists. Press Confirm again to replace it.<span id="overwrite-outfit"></span>'; return; } fetchState({ action: 'saveOutfit', name: name }, true); }); };
  document.onkeydown = function (e) {
    if (!hidden(id('shade'))) {
      if (e.keyCode === 27) { e.preventDefault(); closeDialog(); }
      if (e.keyCode === 9) { var focus = id('dialog').querySelectorAll('button,input'), first = focus[0], last = focus[focus.length - 1]; if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); } else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); } }
    }
  };
  var controls = id('model-controls').querySelectorAll('[data-control]');
  for (var c = 0; c < controls.length; c++) (function (button) {
    var command = button.getAttribute('data-control');
    if (command === 'left' || command === 'right') { button.onmousedown = function (e) { e.preventDefault(); nativeControl(command,1); }; button.onmouseup = button.onmouseleave = function () { nativeControl(command,0); }; }
    else button.onclick = function () { nativeControl(command,1); };
  }(controls[c]));
  window.onresize = updatePreview;
  window.onblur = function () { nativeControl('left',0); };
  fetchState();
}());
