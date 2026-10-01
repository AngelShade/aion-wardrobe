"""Check Wardrobe confirmations and layout in the client's actual WebKit.

Uses an isolated HTTP fixture. No game process, account, or production database
is accessed. Run with Python 3.12 and --browser-bin <client>/bin64.
"""
import argparse
import ctypes as c
import http.server
import json
import os
from pathlib import Path
import threading
import time
from urllib.parse import parse_qs, urlparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser-bin', type=Path, required=True)
    args = parser.parse_args()
    media = Path(__file__).resolve().parents[2] / 'game-server/config/wardrobe/media'
    appearance = dict(item=114100001, name='Boots Appearance', category='Armor', type='Cloth Boots', group='RB_SHOES', slots=128,
                      quality='COMMON', unlocked=False, sources=[dict(object=101, name='Boots Appearance')])
    equipment = dict(object=102, item=114100002, skin=114100002, name='Equipped Boots', type='Plate Boots', group='PL_SHOES', slots=128,
                     equipped=True, slot=128)
    state = dict(character='Fixture', total=1, unlocked=0, tickets=5, categories=['All', 'Armor'], equipment=[equipment],
                 skins=[appearance], results=1, page=1, pages=1, target=102, outfits=[], request='fixture-only', notice='')
    posts = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def send(self, data, content='application/json'):
            self.send_response(200)
            self.send_header('Content-Type', content)
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(data if isinstance(data, bytes) else data.encode())

        def do_GET(self):
            path = urlparse(self.path).path
            if path == '/market/wardrobe':
                self.send((media / 'wardrobe.html').read_bytes(), 'text/html')
            elif path.startswith('/market/wardrobe/media/'):
                file = media / path.rsplit('/', 1)[1]
                self.send(file.read_bytes(), 'text/css' if file.suffix == '.css' else 'application/javascript')
            elif path == '/market/wardrobe/state':
                self.send(json.dumps(state))
            else:
                self.send_error(404)

        def do_POST(self):
            form = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
            assert form['request'] == ['fixture-only']
            posts.append(form)
            if form['action'] == ['unlock']:
                assert form['skin'] == ['114100001'] and form['source'] == ['101']
                appearance['unlocked'] = True
                state.update(unlocked=1, tickets=4, notice='Appearance unlocked.')
            self.send(json.dumps(state))

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    directory = os.add_dll_directory(str(args.browser_bin.resolve()))
    lib = c.CDLL(str(args.browser_bin.resolve() / 'Awesomium.dll'))
    ptr = c.c_void_p

    def api(name, result, *parameters):
        f = getattr(lib, name)
        f.restype, f.argtypes = result, list(parameters)
        return f

    initialize = api('awe_webcore_initialize_default', None)
    update = api('awe_webcore_update', None)
    shutdown = api('awe_webcore_shutdown', None)
    create = api('awe_webcore_create_webview', ptr, c.c_int, c.c_int, c.c_bool)
    destroy = api('awe_webview_destroy', None, ptr)
    make = api('awe_string_create_from_wide', ptr, c.c_wchar_p, c.c_size_t)
    free = api('awe_string_destroy', None, ptr)
    load = api('awe_webview_load_url', None, ptr, ptr, ptr, ptr, ptr)
    evaluate = api('awe_webview_execute_javascript_with_result', ptr, ptr, ptr, ptr, c.c_int)
    text = api('awe_jsvalue_to_string', ptr, ptr)
    utf8 = api('awe_string_to_utf8', c.c_size_t, ptr, ptr, c.c_size_t)
    jsfree = api('awe_jsvalue_destroy', None, ptr)
    initialize()
    empty = make('', 0)
    views = []

    def js(view, code):
        script = make(code, len(code))
        value = evaluate(view, script, empty, 1000)
        free(script)
        if not value:
            return ''
        string = text(value)
        buffer = c.create_string_buffer(8192)
        utf8(string, buffer, len(buffer))
        free(string)
        jsfree(value)
        return buffer.value.decode()

    def pump(seconds=.2):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            update()
            time.sleep(.02)

    try:
        for width, height in [(1024, 740), (1920, 1052), (3440, 1412)]:
            view = create(width, height, False)
            views.append(view)
            url = f'http://127.0.0.1:{server.server_port}/market/wardrobe?session_id=fixture'
            s = make(url, len(url))
            load(view, s, empty, empty, empty)
            free(s)
            until = time.monotonic() + 12
            while time.monotonic() < until:
                pump(.1)
                if js(view, "document.getElementById('status')&&document.getElementById('status').textContent") in ('Wardrobe ready.', 'Appearance unlocked.'):
                    break
            else:
                raise AssertionError('Wardrobe fixture did not load')
            geometry = json.loads(js(view, "(function(){var a={},names=['native-preview','model-controls','preview-actions','detail'];for(var i=0;i<names.length;i++){var r=document.getElementById(names[i]).getBoundingClientRect();a[names[i]]={top:r.top,bottom:r.bottom,height:r.height};}return JSON.stringify(a);}())"))
            assert geometry['native-preview']['height'] >= 150, geometry
            assert geometry['native-preview']['bottom'] <= geometry['model-controls']['top'], geometry
            assert geometry['model-controls']['bottom'] <= geometry['preview-actions']['top'], geometry
            assert geometry['preview-actions']['bottom'] <= geometry['detail']['top'], geometry
            actions = json.loads(js(view, "(function(){var b=document.getElementById('remove-locked-preview');b.removeAttribute('hidden');var a=document.getElementById('preview-actions').getBoundingClientRect(),r=b.getBoundingClientRect(),p=document.getElementById('apply-changes').getBoundingClientRect();b.setAttribute('hidden','');return JSON.stringify({left:a.left,right:a.right,top:a.top,bottom:a.bottom,removeLeft:r.left,removeRight:r.right,removeBottom:r.bottom,applyLeft:p.left});}())"))
            assert actions['left'] <= actions['removeLeft'] and actions['removeRight'] <= actions['applyLeft'] and actions['removeBottom'] <= actions['bottom'], actions
            js(view, "window.previewCalls=[];window.pollCalls=0;window.AionObject={WardrobePreview:function(){previewCalls.push([].slice.call(arguments));},WardrobePoll:function(){pollCalls++;var token=previewCalls[previewCalls.length-1][5];setTimeout(function(){WardrobePreviewState(true,token);},20);},WardrobeControl:function(){}};document.getElementById('reset-preview').onclick()")
            pump(.12)
            assert js(view, "previewCalls.length+','+previewCalls[0].length") == '1,6'
            js(view, "WardrobePreviewState(false,previewCalls[0][5]-1)")
            assert js(view, "document.getElementById('preview-message').textContent") == 'Loading character preview…'
            pump(.4)
            assert js(view, "document.getElementById('preview-message').textContent") == ''
            polls = js(view, 'pollCalls')
            pump(.4)
            assert js(view, 'pollCalls') == polls, 'Polling continued after acknowledgement'
            js(view, "document.getElementById('outfits-tab').onclick()")
            assert js(view, "getComputedStyle(document.getElementById('outfits-view'),null).display") != 'none'
            assert js(view, "getComputedStyle(document.getElementById('collection-view'),null).display") == 'none'
            js(view, "document.getElementById('save-outfit').onclick()")
            assert js(view, "getComputedStyle(document.getElementById('shade'),null).display") != 'none'
            js(view, "document.getElementById('cancel').onclick();document.getElementById('collection-tab').onclick()")
            assert js(view, "getComputedStyle(document.getElementById('shade'),null).display") == 'none'
            js(view, "document.querySelector('.skin-card').onclick()")
            js(view, "document.getElementById('preview').onclick()")
            pump(.4)
            drafted = js(view, "document.querySelector('.equipment-item').className")
            assert 'drafted' in drafted, (drafted, js(view, "document.getElementById('status').textContent"))
            assert js(view, "previewCalls[previewCalls.length-1][0]") == str(appearance['item']), 'Draft appearance did not reach native preview'
            if not posts:
                js(view, "document.getElementById('unlock').onclick()")
                modal = json.loads(js(view, "(function(){var r=document.getElementById('dialog').getBoundingClientRect();return JSON.stringify({display:getComputedStyle(document.getElementById('shade'),null).display,left:r.left,top:r.top,right:r.right,bottom:r.bottom});}())"))
                assert modal['display'] != 'none' and modal['left'] >= 0 and modal['top'] >= 0 and modal['right'] <= width and modal['bottom'] <= height, modal
                js(view, "document.getElementById('confirm').onclick();document.getElementById('confirm').onclick()")
                pump(.5)
                assert len(posts) == 1 and state['tickets'] == 4 and appearance['unlocked'], posts
                assert js(view, "getComputedStyle(document.getElementById('shade'),null).display") == 'none'
                assert js(view, "document.getElementById('tickets').textContent") == '4'
            print(f'PASS actual Aion WebKit {width}x{height}: confirmations, tabs, preview controls, queued acknowledgement and polling stops')
            if width == 3440:
                headwear = dict(item=125003948, name='Unlocked Headwear', category='Headwear', type='Headwear', group='CL_HEADS', slots=4,
                                quality='EPIC', unlocked=True, sources=[])
                helmet = dict(object=103, item=125004152, skin=125004152, name='Equipped Helmet', type='Headwear', group='HEAD', slots=4,
                              equipped=True, slot=4)
                state.update(skins=[headwear], equipment=[helmet], target=103, categories=['All','Headwear'])
                js(view, "document.getElementById('reset-preview').onclick();document.getElementById('refresh').onclick()")
                pump(.5)
                js(view, "document.querySelector('.skin-card').onclick();document.getElementById('preview').onclick()")
                pump(.5)
                assert js(view, "previewCalls[previewCalls.length-1][0]") == str(headwear['item']), 'Headwear missing from preview request'
                assert js(view, "document.getElementById('apply-changes').disabled?'disabled':'enabled'") == 'enabled', 'Unlocked headwear must apply to a helmet'
                print('PASS: unlocked appearance headwear can preview and apply to a stat helmet')
                appearance['unlocked'] = False
                state.update(skins=[appearance], equipment=[helmet,equipment], target=102)
                js(view, "document.getElementById('refresh').onclick()")
                pump(.5)
                js(view, "document.querySelector('.skin-card').onclick();document.getElementById('preview').onclick()")
                pump(.5)
                assert js(view, "document.getElementById('apply-changes').disabled?'disabled':'enabled'") == 'disabled', 'Locked preview must not bypass account unlocks'
                assert appearance['name'] in js(view, "document.getElementById('status').textContent"), 'Apply blocker must name the locked skin'
                assert js(view, "getComputedStyle(document.getElementById('remove-locked-preview'),null).display") != 'none'
                js(view, "document.getElementById('remove-locked-preview').onclick()")
                pump(.5)
                assert js(view, "document.getElementById('apply-changes').disabled?'disabled':'enabled'") == 'enabled', 'Unlocked helmet must apply after removing a locked preview'
                js(view, "document.getElementById('apply-changes').onclick();document.getElementById('confirm').onclick()")
                pump(.5)
                changes = json.loads(posts[-1]['changes'][0])
                assert changes == [dict(object=103,skin=headwear['item'],expected=helmet['skin'])], changes
                print('PASS: locked preview names its blocker; removing it preserves and submits the unlocked helmet change')
                wing = dict(item=187000001, name='Wing Appearance', category='Wings', type='Wings', group='WING', slots=32768,
                            quality='COMMON', unlocked=True, sources=[])
                state.update(skins=[wing], equipment=[], target=0, categories=['All','Wings'])
                js(view, "window.wingControls=[];AionObject.WardrobeControl=function(name,pressed){wingControls.push(name+':'+pressed);};document.getElementById('reset-preview').onclick();document.getElementById('refresh').onclick()")
                pump(.5)
                js(view, "document.querySelector('.skin-card').onclick();document.getElementById('preview').onclick()")
                pump(.5)
                assert js(view, "previewCalls[previewCalls.length-1][0]") == '187000001', 'Wings missing from preview request'
                assert 'wings:1' in js(view, "wingControls.join(',')"), 'Native wing pose not requested'
                assert js(view, "document.getElementById('apply-changes').disabled?'disabled':'enabled'") == 'disabled', 'Preview-only wings must not apply without equipment'
                print('PASS: wings can be tried on without equipped wings; native wing pose requested; applying still requires equipment')
            destroy(view)
            views.remove(view)
        print('PASS: Unlock Appearance submits once, consumes one fixture ticket and refreshes the collection')
    finally:
        for view in views:
            destroy(view)
        free(empty)
        shutdown()
        directory.close()
        server.shutdown()


if __name__ == '__main__':
    main()
