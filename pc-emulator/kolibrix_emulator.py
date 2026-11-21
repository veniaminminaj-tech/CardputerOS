#!/usr/bin/env python3
"""KolibriX PC Emulator — desktop/UI simulator for CardputerOS."""
from __future__ import annotations
import sys, time
from pathlib import Path
import pygame

W, H, SCALE, FPS = 320, 240, 3, 30
STATUS_H, TASKBAR_H = 14, 22
DESK=(8,16,8); DGRAY=(32,32,32); WHITE=(245,245,245); GRAY=(145,145,145)
GREEN=(0,235,100); CYAN=(0,220,230); YELLOW=(255,225,70); BLACK=(0,0,0)
TITLE_ON=(25,105,155)
ROOT=Path(__file__).resolve().parent
SD_ROOT=ROOT/"sdcard"
for folder in ("apps","documents","downloads","root"):
    (SD_ROOT/folder).mkdir(parents=True,exist_ok=True)
welcome=SD_ROOT/"documents"/"WELCOME.txt"
if not welcome.exists():
    welcome.write_text("Welcome to KolibriX PC Emulator!\nThis is a UI simulator, not ESP32 firmware emulation.\nPut test files in this sdcard folder.\n",encoding="utf-8")

class KolibriXEmulator:
    def __init__(self):
        pygame.init()
        pygame.display.set_caption("KolibriX PC Emulator")
        self.window=pygame.display.set_mode((W*SCALE,H*SCALE),pygame.RESIZABLE)
        self.canvas=pygame.Surface((W,H))
        self.clock=pygame.time.Clock()
        self.font=pygame.font.Font(None,10)
        self.big=pygame.font.Font(None,17)
        self.running=True; self.user=""; self.password=""; self.login_field=0; self.login_error=""
        self.screen="login"; self.selection=0; self.brightness=200; self.settings_sel=0
        self.apps=[
            ("Settings",(120,120,120)),("Browser",(20,150,210)),("Shell",(0,105,50)),
            ("Telegram",(35,145,200)),("Device",(30,175,90)),("Files",(0,145,210))]
        self.file_path=Path("/"); self.file_items=[]; self.file_selection=0
        self.file_view=None; self.file_view_name=""
        self.term_input=""; self.term_lines=["KolibriX 0.1 PC simulator","Type 'help' for commands.",""]
        self.term_history=[]; self.toast="PC simulator | root / root"
        self.browser_url=""; self.browser_message="Browser is a UI placeholder in this build."

    def text(self,x,y,value,color=WHITE,font=None):
        s=(font or self.font).render(str(value).encode("ascii","replace").decode(),False,color)
        self.canvas.blit(s,(x,y))
    def clip(self,s,n):
        s=str(s); return s if len(s)<=n else s[:max(0,n-1)]+"."

    def login(self):
        self.canvas.fill(BLACK); self.text(12,27,"KOLIBRIX",GREEN,self.big)
        self.text(12,48,"CardputerOS login")
        self.text(12,78,("> " if self.login_field==0 else "  ")+"login:    "+self.user+("_" if self.login_field==0 else ""))
        self.text(12,99,("> " if self.login_field==1 else "  ")+"password: "+"*"*len(self.password)+("_" if self.login_field==1 else ""))
        self.text(12,132,self.login_error or "Demo account: root / root",YELLOW)
        self.text(12,160,"Tab: switch field"); self.text(12,173,"Enter: next / login")
        self.text(12,220,"KolibriX PC Emulator",GRAY)

    def status(self):
        pygame.draw.rect(self.canvas,DGRAY,(0,0,W,STATUS_H))
        self.text(4,3,"KolibriX  "+(self.user or "locked"))
        right="PC SIM  "+time.strftime("%H:%M")
        self.text(W-len(right)*6-4,3,right,CYAN)

    def desktop(self):
        self.canvas.fill(DESK)
        for y in range(STATUS_H,H-TASKBAR_H,4):
            pygame.draw.line(self.canvas,(16,42,38) if y<112 else (8,28,24),(0,y),(W,y))
        for i,(name,color) in enumerate(self.apps):
            col,row=i%4,i//4; x=col*80+26; y=STATUS_H+14+row*86
            r=pygame.Rect(x,y,28,28); pygame.draw.rect(self.canvas,color,r,border_radius=5)
            if i==self.selection: pygame.draw.rect(self.canvas,WHITE,r.inflate(4,4),1,border_radius=6)
            self.text(x+10,y+9,name[0].upper())
            self.text(col*80+max(2,(80-len(name)*6)//2),y+35,self.clip(name,12))
        y=H-TASKBAR_H
        pygame.draw.rect(self.canvas,(65,65,65),(0,y,W,TASKBAR_H))
        pygame.draw.line(self.canvas,WHITE,(0,y),(W,y))
        pygame.draw.rect(self.canvas,(195,195,195),(4,y+3,66,16),border_radius=3)
        self.text(10,y+7,"K  Start",BLACK)
        pygame.draw.rect(self.canvas,(30,30,30),(76,y+3,84,16),border_radius=3)
        self.text(82,y+7,"KolibriX")
        self.text(170,y+7,"SD: ready",GREEN)

    def window_frame(self,title):
        self.canvas.fill(DESK)
        pygame.draw.rect(self.canvas,TITLE_ON,(0,STATUS_H,W,12))
        self.text(4,STATUS_H+2,title)
        pygame.draw.rect(self.canvas,BLACK,(0,STATUS_H+12,W,H-STATUS_H-12))
        pygame.draw.rect(self.canvas,WHITE,(0,STATUS_H,W,H-STATUS_H-1),1)

    def reload_files(self):
        target=SD_ROOT/str(self.file_path).lstrip("/")
        try: self.file_items=sorted(target.iterdir(),key=lambda p:(not p.is_dir(),p.name.lower()))
        except OSError: self.file_items=[]
        self.file_selection=min(self.file_selection,max(0,len(self.file_items)-1))
    def open_files(self):
        self.file_path=Path("/"); self.file_view=None; self.reload_files(); self.screen="files"
    def draw_files(self):
        self.window_frame("Files"); y0=STATUS_H+14
        self.text(3,y0,self.file_view_name if self.file_view is not None else self.file_path.as_posix(),CYAN)
        if self.file_view is not None:
            for i,line in enumerate(self.file_view[self.file_selection:self.file_selection+24]):
                self.text(3,y0+11+i*8,self.clip(line,52))
            self.text(3,225,"Backspace: back | Up/Down: scroll",GRAY); return
        if not self.file_items: self.text(8,y0+25,"(empty folder)")
        for i,item in enumerate(self.file_items[:23]):
            sel=i==self.file_selection; yy=y0+12+i*8
            if sel: pygame.draw.rect(self.canvas,WHITE,(1,yy-1,W-2,9))
            self.text(4,yy,self.clip(item.name+("/" if item.is_dir() else ""),50),
                      BLACK if sel else (CYAN if item.is_dir() else WHITE))
        self.text(3,225,"Enter: open | Backspace: parent",GRAY)

    def term_write(self,line):
        self.term_lines.extend(str(line).splitlines() or [""]); self.term_lines=self.term_lines[-300:]
    def command(self,raw):
        cmd=raw.strip(); self.term_write("root@kolibrix:~$ "+cmd)
        if not cmd: return
        self.term_history.append(cmd); p=cmd.split(); name=p[0].lower(); args=p[1:]
        if name in ("help","?"):
            self.term_write("help, clear, ls, pwd, whoami, uname, date, uptime,")
            self.term_write("echo TEXT, cat FILE, reboot, exit")
        elif name=="clear": self.term_lines=[]
        elif name=="ls":
            try:
                for item in sorted(SD_ROOT.iterdir()): self.term_write(item.name+("/" if item.is_dir() else ""))
            except OSError as e: self.term_write(e)
        elif name=="pwd": self.term_write("/")
        elif name=="whoami": self.term_write(self.user or "root")
        elif name=="uname": self.term_write("KolibriX 0.1 PC-SIM ESP32-S3 UI")
        elif name=="date": self.term_write(time.strftime("%a %b %d %H:%M:%S %Y"))
        elif name=="uptime": self.term_write("PC simulator is running")
        elif name=="echo": self.term_write(" ".join(args))
        elif name=="cat" and args:
            try: self.term_write((SD_ROOT/args[0].lstrip("/")).read_text(encoding="utf-8",errors="replace"))
            except OSError as e: self.term_write("cat: "+str(e))
        elif name=="reboot": self.screen="desktop"
        elif name=="exit": self.screen="desktop"
        else: self.term_write(name+": command not implemented in PC simulator")
    def draw_terminal(self):
        self.window_frame("Terminal")
        for i,line in enumerate(self.term_lines[-24:]):
            self.text(3,STATUS_H+14+i*8,self.clip(line,52),GREEN)
        self.text(3,225,self.clip("root@kolibrix:~$ "+self.term_input+"_",52))

    def draw_settings(self):
        self.window_frame("Settings")
        rows=["Brightness   < "+str(self.brightness)+" >","Storage      sdcard/ (local folder)",
              "Wi-Fi        not connected (simulated)","Sign out","About"]
        for i,label in enumerate(rows):
            y=STATUS_H+32+i*17; selected=i==self.settings_sel
            if selected: pygame.draw.rect(self.canvas,WHITE,(2,y-2,W-4,12))
            self.text(6,y,label,BLACK if selected else WHITE)
        self.text(4,225,"W/S select | A/D change | Enter",GRAY)
    def draw_device(self):
        self.window_frame("Device")
        lines=["Target: M5Stack Cardputer ADV","CPU: ESP32-S3 (simulated)","Display: 320 x 240",
               "Panel: ST7789 / UI preview","Keyboard: PC keyboard","SD: local sdcard/ folder",
               "Wi-Fi: simulated / offline","Firmware: not executed"]
        for i,line in enumerate(lines): self.text(5,STATUS_H+18+i*13,line,GREEN)
    def draw_browser(self):
        self.window_frame("Browser"); self.text(4,31,"URL: "+self.clip(self.browser_url or "about:kolibrix",48),CYAN)
        self.text(4,56,self.browser_message); self.text(4,225,"Network access not implemented",GRAY)
    def draw_telegram(self):
        self.window_frame("Telegram"); self.text(5,35,"Telegram client is not connected.",YELLOW)
        self.text(5,50,"Configure bot credentials on real device.")
        self.text(5,225,"Esc / Ctrl+W: close",GRAY)

    def draw(self):
        screens={"login":self.login,"desktop":self.desktop,"files":self.draw_files,
                 "terminal":self.draw_terminal,"settings":self.draw_settings,
                 "device":self.draw_device,"browser":self.draw_browser,"telegram":self.draw_telegram}
        screens.get(self.screen,self.desktop)(); self.status()
        if self.toast and self.screen=="desktop": self.text(4,H-TASKBAR_H-9,self.toast,YELLOW)

    def open_app(self,name):
        if name=="Files": self.open_files()
        else:
            self.screen=name.lower()
            if name=="Settings": self.settings_sel=0
    def close_top(self): self.screen="desktop"

    def keydown(self,e):
        if self.screen=="login":
            if e.key==pygame.K_TAB: self.login_field=1-self.login_field
            elif e.key==pygame.K_RETURN:
                if self.login_field==0: self.login_field=1
                elif self.user=="root" and self.password=="root":
                    self.screen="desktop"; self.login_error=""; self.toast="Welcome to KolibriX"
                else: self.login_error="Login incorrect"; self.password=""; self.login_field=0
            elif e.key==pygame.K_BACKSPACE:
                if self.login_field==0: self.user=self.user[:-1]
                else: self.password=self.password[:-1]
            elif e.unicode and e.unicode.isprintable():
                if self.login_field==0 and len(self.user)<24: self.user+=e.unicode
                elif self.login_field==1 and len(self.password)<24: self.password+=e.unicode
            return
        if e.key==pygame.K_ESCAPE or (e.key==pygame.K_w and e.mod&pygame.KMOD_CTRL):
            self.close_top(); return
        if e.key==pygame.K_TAB and e.mod&pygame.KMOD_ALT:
            self.selection=(self.selection+1)%len(self.apps); return
        if e.key==pygame.K_t and e.mod&pygame.KMOD_CTRL and e.mod&pygame.KMOD_ALT:
            self.screen="terminal"; return
        if self.screen=="desktop":
            if e.key in (pygame.K_LEFT,pygame.K_a): self.selection=max(0,self.selection-1)
            elif e.key in (pygame.K_RIGHT,pygame.K_d): self.selection=min(len(self.apps)-1,self.selection+1)
            elif e.key in (pygame.K_UP,pygame.K_w): self.selection=max(0,self.selection-4)
            elif e.key in (pygame.K_DOWN,pygame.K_s): self.selection=min(len(self.apps)-1,self.selection+4)
            elif e.key in (pygame.K_RETURN,pygame.K_SPACE): self.open_app(self.apps[self.selection][0])
        elif self.screen=="terminal":
            if e.key==pygame.K_RETURN: self.command(self.term_input); self.term_input=""
            elif e.key==pygame.K_BACKSPACE: self.term_input=self.term_input[:-1]
            elif e.key==pygame.K_UP and self.term_history: self.term_input=self.term_history[-1]
            elif e.unicode and e.unicode.isprintable() and not(e.mod&pygame.KMOD_CTRL):
                if len(self.term_input)<180: self.term_input+=e.unicode
        elif self.screen=="files":
            if self.file_view is not None:
                if e.key==pygame.K_BACKSPACE: self.file_view=None
                elif e.key==pygame.K_UP: self.file_selection=max(0,self.file_selection-1)
                elif e.key==pygame.K_DOWN: self.file_selection=min(max(0,len(self.file_view)-1),self.file_selection+1)
            else:
                if e.key in (pygame.K_UP,pygame.K_w): self.file_selection=max(0,self.file_selection-1)
                elif e.key in (pygame.K_DOWN,pygame.K_s): self.file_selection=min(max(0,len(self.file_items)-1),self.file_selection+1)
                elif e.key==pygame.K_BACKSPACE and self.file_path!=Path("/"):
                    self.file_path=self.file_path.parent; self.reload_files()
                elif e.key==pygame.K_RETURN and self.file_items:
                    item=self.file_items[self.file_selection]
                    if item.is_dir(): self.file_path=self.file_path/item.name; self.reload_files()
                    else:
                        try:
                            self.file_view=item.read_text(encoding="utf-8",errors="replace")[:12000].splitlines() or [""]
                            self.file_view_name=item.name; self.file_selection=0
                        except OSError as ex: self.file_view=[str(ex)]; self.file_view_name=item.name
        elif self.screen=="settings":
            if e.key in (pygame.K_UP,pygame.K_w): self.settings_sel=max(0,self.settings_sel-1)
            elif e.key in (pygame.K_DOWN,pygame.K_s): self.settings_sel=min(4,self.settings_sel+1)
            elif e.key in (pygame.K_LEFT,pygame.K_a) and self.settings_sel==0: self.brightness=max(16,self.brightness-16)
            elif e.key in (pygame.K_RIGHT,pygame.K_d) and self.settings_sel==0: self.brightness=min(255,self.brightness+16)
            elif e.key==pygame.K_RETURN:
                if self.settings_sel==3:
                    self.user=""; self.password=""; self.screen="login"; self.login_field=0
                elif self.settings_sel==4: self.screen="device"

    def mouse_click(self,pos):
        x,y=pos
        if self.screen=="desktop" and STATUS_H<y<H-TASKBAR_H:
            index=min(5,max(0,y>=132)*4+min(3,x//80))
            self.selection=index; self.open_app(self.apps[index][0])
        elif self.screen!="login" and x>W-16 and y<STATUS_H+14: self.close_top()

    def run(self):
        while self.running:
            for e in pygame.event.get():
                if e.type==pygame.QUIT: self.running=False
                elif e.type==pygame.KEYDOWN: self.keydown(e)
                elif e.type==pygame.MOUSEBUTTONDOWN and e.button==1:
                    self.mouse_click((e.pos[0]*W//self.window.get_width(),e.pos[1]*H//self.window.get_height()))
            self.draw()
            scaled=pygame.transform.scale(self.canvas,self.window.get_size())
            self.window.blit(scaled,(0,0)); pygame.display.flip(); self.clock.tick(FPS)
        pygame.quit()

if __name__=="__main__":
    try: KolibriXEmulator().run()
    except Exception as exc:
        print("KolibriX Emulator error:",exc,file=sys.stderr); raise
