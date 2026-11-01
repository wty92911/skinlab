"""Compact auto-connect/dropdown flow in an owned WARP process only."""
import json
from pathlib import Path
import sys
import time
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from skinlab.compact_gui import CompactApp

checks=[];error='';app=None
def check(name,value):
    checks.append({'name':name,'passed':bool(value)})
    if not value:raise AssertionError(name)
def wait(predicate,seconds=15):
    end=time.monotonic()+seconds
    while not predicate() and time.monotonic()<end:app.update();time.sleep(.015)
    app.update();return predicate()

try:
    app=CompactApp(visible=False,fixture=True,hotkey=False)
    check('auto_connects_only_owned_fixture',wait(lambda:app.connected) and app.session.last['model']=='Fixture')
    check('automatic_current_hero_display',app.hero.get()=='自建演示')
    check('skin_selection_is_enabled',str(app.skins['state'])=='readonly')
    def widgets(root):
        return [x for child in root.winfo_children() for x in [child,*widgets(child)]]
    check('no_manual_action_buttons',all(x.winfo_class()!='TButton' for x in widgets(app)))
    app.skins.current(1);app.select_skin()
    check('dropdown_selection_applies_without_button',wait(lambda:not app.busy and app.confirmed_skin==1))
    app.withdraw()
    check('hiding_window_preserves_application',app.session.command('status')['current_skin']==1)
    app.skins.current(0);app.select_skin()
    check('default_skin_selection_applies',wait(lambda:not app.busy and app.confirmed_skin==0))
    app.skins.current(1);app.select_skin();app.skins.current(0);app.select_skin()
    check('rapid_selection_finishes_on_latest_choice',wait(lambda:not app.busy and app.confirmed_skin==0 and app.desired_skin==0))
    app.skins.current(1);app.select_skin();wait(lambda:not app.busy and app.confirmed_skin==1)
    app.close_window();wait(lambda:app.closing)
    check('closing_restores_and_stops',app.session.last['state']=='stopped' and app.session.last['current_skin']==0)
    app=None
    app=CompactApp(visible=False,automatic=False,hotkey=False)
    app.handle_match([],None);app.update()
    check('no_game_shows_open_game_prompt',app.hero.get()=='请先打开游戏' and app.session.process is None and str(app.skins['state'])=='disabled')
except Exception as failure:error=str(failure)
finally:
    if app:
        if app.session.process:
            try:app.session.close()
            except Exception:pass
        try:app.finish_close()
        except Exception:pass
print(json.dumps({'status':'passed' if not error else 'failed','checks':len(checks),
                  'passed':sum(c['passed'] for c in checks),'error':error}))
raise SystemExit(1 if error else 0)
