"""Player-facing compact launcher: automatic match, dropdown apply, Insert hide."""
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk
from skinlab import __version__
from skinlab.active_game import current_hero
from skinlab.catalog import load_catalog
from skinlab.launcher import InsertHotkey,game_pids
from skinlab.native_controller import NativeSession


class CompactApp(tk.Tk):
    def __init__(self,visible=True,fixture=False,automatic=True,hotkey=True):
        super().__init__()
        if not visible:self.withdraw()
        self.title('SkinLab '+__version__);self.geometry('410x205');self.resizable(False,False)
        self.fixture=fixture;self.automatic=automatic;self.session=NativeSession()
        self.catalog=load_catalog()
        self.events=queue.Queue();self.busy=False;self.connected=False;self.scanning=False
        self.target=None;self.seen_at=0.;self.retry_at=0.;self.blocked_pid=None;self.model=None
        self.desired_skin=None;self.confirmed_skin=None;self.rows=[];self.hotkey=None;self.closing=False
        self.hero=tk.StringVar(value='请先打开游戏');self.skin=tk.StringVar();self.status=tk.StringVar(value='等待对局 · Insert 显示／隐藏')
        frame=ttk.Frame(self,padding=18);frame.pack(fill='both',expand=True)
        ttk.Label(frame,textvariable=self.hero,font=('Segoe UI',16,'bold')).pack(anchor='w',pady=(0,12))
        self.skins=ttk.Combobox(frame,textvariable=self.skin,state='disabled',width=43)
        self.skins.pack(fill='x');self.skins.bind('<<ComboboxSelected>>',lambda event:self.select_skin())
        ttk.Label(frame,textvariable=self.status,wraplength=370).pack(anchor='w',pady=(12,0))
        self.protocol('WM_DELETE_WINDOW',self.close_window);self.after(50,self.poll)
        if hotkey:
            try:self.hotkey=InsertHotkey(lambda:self.events.put(('toggle',)))
            except OSError:self.status.set('Insert 被其他程序占用 · 可用最小化收起窗口')
        if automatic:self.after(120,self.tick)

    def tick(self):
        if self.closing:return
        if not self.scanning:
            self.scanning=True
            def scan():
                try:
                    if self.fixture:result=([-1],{'model':'Fixture','display_name':'自建演示'})
                    else:
                        pids=game_pids();result=(pids,current_hero() if len(pids)==1 else None)
                    self.events.put(('match',*result))
                except Exception as error:self.events.put(('scan_error',str(error)))
            threading.Thread(target=scan,daemon=True).start()
        if self.automatic:self.after(1500,self.tick)

    def run_action(self,kind,operation):
        if self.busy:return
        self.busy=True
        def run():
            try:self.events.put(('result',kind,operation(),None))
            except Exception as error:self.events.put(('result',kind,None,error))
        threading.Thread(target=run,daemon=True).start()

    def show_model(self,model):
        self.model=model
        if model=='Fixture':row={'name':'自建演示','skins':[{'skin_id':0,'name':'原始皮肤'},{'skin_id':1,'name':'测试皮肤 1'}]}
        else:row=next((x for x in self.catalog['champions'] if x['id'].casefold()==model.casefold()),None)
        self.hero.set(row['name'] if row else model)
        self.rows=row['skins'] if row else []
        self.skins['values']=[x['name'] for x in self.rows]
        self.skins['state']='readonly' if self.connected and self.rows else 'disabled'
        index=next((i for i,x in enumerate(self.rows) if x['skin_id']==self.confirmed_skin),0)
        if self.rows:self.skins.current(index)
        else:self.skin.set('等待皮肤目录')

    def select_skin(self):
        index=self.skins.current()
        if not self.connected or not 0<=index<len(self.rows):return
        self.desired_skin=self.rows[index]['skin_id'];self.drive()

    def drive(self):
        if self.connected and not self.busy and self.desired_skin is not None and self.desired_skin!=self.confirmed_skin:
            wanted=self.desired_skin
            self.status.set('正在切换…')
            self.run_action('apply',lambda:self.session.command('apply',wanted))

    def handle_match(self,pids,hero):
        self.scanning=False
        pid=pids[0] if len(pids)==1 else None
        if self.connected:
            if pid!=self.target and not self.busy:
                self.status.set('对局已结束，正在清理…');self.run_action('ended',self.session.close)
            return
        if self.busy:return
        if pid is None:
            self.target=None;self.model=None;self.hero.set('请先打开游戏' if not pids else '请只保留一个对局')
            self.skins['state']='disabled';self.skin.set('等待对局');self.status.set('Insert 显示／隐藏');return
        if pid!=self.target:
            self.target=pid;self.seen_at=time.monotonic();self.blocked_pid=None;self.retry_at=0
        if hero:self.show_model(hero['model'])
        if self.blocked_pid==pid or time.monotonic()<self.retry_at:return
        if not hero and time.monotonic()-self.seen_at<8:
            self.hero.set('等待英雄载入…');return
        self.status.set('正在载入…')
        self.run_action('connect',lambda:self.session.connect(None if self.fixture else pid,fixture=self.fixture))

    def poll(self):
        try:
            while True:
                event=self.events.get_nowait();kind=event[0]
                if kind=='toggle':
                    if self.state() in ('withdrawn','iconic'):self.deiconify();self.lift()
                    else:self.withdraw()
                elif kind=='scan_error':self.scanning=False
                elif kind=='match':self.handle_match(event[1],event[2])
                elif kind=='result':
                    _,action,state,error=event;self.busy=False
                    if error:
                        self.status.set(str(error));self.desired_skin=None
                        if action=='connect':
                            message=str(error)
                            if 'consistent current player' in message or '尚未就绪' in message:
                                self.retry_at=time.monotonic()+5;self.status.set('等待英雄载入…')
                            else:self.blocked_pid=self.target
                        if self.session.last:self.confirmed_skin=self.session.last.get('current_skin',self.confirmed_skin)
                        if self.model:self.show_model(self.model)
                        continue
                    if action=='connect':
                        self.connected=True;self.confirmed_skin=state['current_skin'];self.desired_skin=None
                        self.show_model(state['model']);self.status.set('选择皮肤即生效 · Insert 显示／隐藏')
                    elif action=='apply':
                        self.confirmed_skin=state['current_skin'];self.status.set('已生效 · Insert 显示／隐藏')
                        if self.desired_skin==self.confirmed_skin:self.show_model(state['model'])
                    elif action in ('ended','close'):
                        self.connected=False;self.confirmed_skin=None;self.desired_skin=None
                        if action=='close':self.finish_close();return
                        self.target=None;self.hero.set('请先打开游戏');self.skins['state']='disabled';self.skin.set('等待下一局')
                    self.drive()
        except queue.Empty:pass
        self.after(50,self.poll)

    def close_window(self):
        if self.busy:
            self.status.set('正在完成操作，稍后将恢复并退出…')
            self.after(150,self.close_window);return
        if self.connected:
            self.run_action('close',self.session.close)
        else:self.finish_close()

    def finish_close(self):
        self.closing=True
        if self.hotkey:self.hotkey.close()
        self.destroy()
