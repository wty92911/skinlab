"""Native-session transport and artifact validation. Construction is offline."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading

ROOT=Path(__file__).resolve().parents[1]
GAME=Path('C:/WeGameApps/英雄联盟/Game/League of Legends.exe')
VERIFIED_GAME_SHA256='8dd49307106f45f65288425ce5291b9de4d0ad50e721d9255c0f63b888baf0ee'


class SessionError(RuntimeError):
    def __init__(self,message,state=None):
        super().__init__(message);self.state=state


def friendly_error(message):
    translations={
        'Target integrity exceeds observer integrity':'游戏权限高于启动器，请在离线状态用管理员重新打开',
        'Current model/skin has no verified':'当前英雄或皮肤尚未纳入已验证支持范围',
        'Required session recipe':'关键版本特征未通过检查，未应用皮肤',
        'An observation worker is already running':'本局已有会话或已停止，请在新对局中接入',
        'A session is already active':'本局已有会话，请使用原控制器或在新对局中接入',
        'Requested PID does not have the exact':'该 PID 不是指定安装目录中的对局进程',
        'Target disk SHA256':'游戏文件与已验证版本不一致',
        'Target process has exited':'对局进程已退出',
    }
    for fragment,translated in translations.items():
        if fragment in message:return translated
    return message


def verify_bundle(root=ROOT):
    root=Path(root);pointer=root/'out/current.json'
    bundle=json.loads(pointer.read_text('utf-8-sig'))['bundle'] if pointer.exists() else 'session'
    if not bundle.startswith('session') or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in bundle):raise SessionError('Invalid native bundle')
    manifest=json.loads((root/'out'/bundle/'build-manifest.json').read_text('utf-8-sig'))
    manifest['bundle']=bundle
    for item in manifest['artifacts']:
        if hashlib.sha256((root/'out'/bundle/item['name']).read_bytes()).hexdigest()!=item['sha256']:
            raise SessionError('原生产物校验失败，请重新构建')
    return manifest


class NativeSession:
    def __init__(self,root=ROOT,timeout=15):
        self.root=Path(root);self.timeout=timeout;self.process=None;self.last=None
        self.lines=queue.Queue();self.lock=threading.Lock();self.log=None

    def connect(self,pid=None,fixture=False):
        if self.process is not None:raise SessionError('已有会话，请先恢复并停止')
        manifest=verify_bundle(self.root)
        host=self.root/'out'/manifest['bundle']/'skinlab-observe-host.exe'
        if fixture:command=[str(host),'--fixture-session']
        else:
            if type(pid) is not int or not 0<pid<=0xffffffff:raise ValueError('请填写有效对局进程 PID')
            if hashlib.sha256(GAME.read_bytes()).hexdigest()!=VERIFIED_GAME_SHA256:raise SessionError('游戏版本已变化，当前适配配置不能应用')
            command=[str(host),'--observe-game',str(pid),VERIFIED_GAME_SHA256,'--accept-module-until-exit','--session']
        logs=self.root/'local-data/native/logs';logs.mkdir(parents=True,exist_ok=True)
        self.log=(logs/('session-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%f')+'.jsonl')).open('x',encoding='utf-8')
        self.lines=queue.Queue()
        self.process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
            text=True,encoding='utf-8',bufsize=1,creationflags=subprocess.CREATE_NO_WINDOW)
        responses=self.lines
        def read_lines(process):
            try:
                for line in process.stdout:
                    try:responses.put(json.loads(line))
                    except (ValueError,TypeError):responses.put({'error':'原生控制器返回无效协议数据'})
            finally:responses.put(None)
        threading.Thread(target=read_lines,args=(self.process,),daemon=True).start()
        try:return self._receive(timeout=30)
        except Exception:
            self._close_input()
            try:self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:self.process.terminate()
            self.process=None
            if self.log:self.log.close();self.log=None
            raise

    def _receive(self,timeout=None):
        try:state=self.lines.get(timeout=timeout or self.timeout)
        except queue.Empty:raise SessionError('尚未收到帧线程确认；命令可能在后续帧执行，请恢复或停止',self.last)
        if state is None:raise SessionError('原生控制器已退出',self.last)
        if self.log:
            self.log.write(json.dumps({'at':datetime.now(timezone.utc).isoformat(),'state':state},ensure_ascii=False)+'\n');self.log.flush()
        self.last=state
        if state.get('event')!='session':raise SessionError(friendly_error(state.get('error') or '无法建立原生会话'),state)
        if state.get('error') or state.get('result',0)!=0:
            message=state.get('error') or {5:'该皮肤编号不在当前英雄的目录中',6:'当前角色已变化，请重新接入',9:'恢复尚未完成，请重试恢复'}.get(state.get('result'),f"操作未完成，状态码 {state['result']}")
            raise SessionError(friendly_error(message),state)
        return state

    def command(self,operation,skin=None):
        if operation not in ('apply','restore','stop','status'):raise ValueError('Unknown command')
        if operation=='apply' and (type(skin) is not int or not 0<=skin<=100000):raise ValueError('Invalid skin number')
        with self.lock:
            if not self.process or self.process.poll() is not None:raise SessionError('会话未连接或已退出',self.last)
            text=f'apply {skin}' if operation=='apply' else operation
            self.process.stdin.write(text+'\n');self.process.stdin.flush()
            return self._receive()

    def _close_input(self):
        if self.process and self.process.stdin:
            try:self.process.stdin.close()
            except OSError:pass

    def close(self):
        if not self.process:return self.last
        if self.process.poll() is None and (not self.last or self.last.get('state') not in ('stopped','target_exited')):
            try:self.command('stop')
            except SessionError:
                if not self.last or self.last.get('state')!='target_exited':raise
        self._close_input()
        try:self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:raise SessionError('停止已请求，但控制器尚未退出',self.last)
        self.process=None
        if self.log:self.log.close();self.log=None
        return self.last
