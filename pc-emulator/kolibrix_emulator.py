#!/usr/bin/env python3
"""KolibriX PC Emulator — desktop/UI simulator for CardputerOS."""
from __future__ import annotations
import sys, time
from pathlib import Path
import pygame

W, H, SCALE, FPS = 320, 240, 4, 30
STATUS_H, TASKBAR_H = 14, 22
DESK=(18,55,112); DGRAY=(24,35,58); WHITE=(245,248,255); GRAY=(165,185,215)
GREEN=(0,235,100); CYAN=(0,220,230); YELLOW=(255,225,70); BLACK=(0,0,0)
TITLE_ON=(35,95,175)
ROOT=Path(sys.executable).resolve().parent if getattr(sys,"frozen",False) else Path(__file__).resolve().parent
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
        self.window=pygame.display.set_mode((1280,720),pygame.RESIZABLE)
        # Render the existing 320x240 layout at 4x resolution.
        self.canvas=pygame.Surface((W*SCALE,H*SCALE))
        self.clock=pygame.time.Clock()
        font_path=self.find_font()
        self.font=pygame.font.Font(font_path,10*SCALE)
        self.big=pygame.font.Font(font_path,17*SCALE)
        self.running=True; self.user="root"; self.password=""; self.login_field=0; self.login_error=""
        self.screen="controls"; self.control_choice=0; self.control_mode="keyboard"; self.selection=0; self.brightness=200; self.settings_sel=0
        self.apps=[
            ("Settings",(120,120,120)),("Browser",(20,150,210)),("Shell",(0,105,50)),
            ("Telegram",(35,145,200)),("Device",(30,175,90)),("Files",(0,145,210))]
        self.file_path=Path("/"); self.file_items=[]; self.file_selection=0
        self.file_view=None; self.file_view_name=""
        self.term_input=""; self.term_lines=["KolibriX 0.1 PC simulator","Type 'help' for commands.",""]
        self.term_history=[]; self.toast="PC simulator | root (no password)"
        self.browser_url=""; self.browser_message="Browser is a UI placeholder in this build."

    @staticmethod
    def find_font():
        candidates = [
            Path("C:/Windows/Fonts/arial.ttf"),
            Path("C:/Windows/Fonts/segoeui.ttf"),
            Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
            Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
        ]
        for candidate in candidates:
            if candidate.exists():
                return str(candidate)
        return None

    def text(self,x,y,value,color=WHITE,font=None):
        # Keep Unicode text and use antialiasing for crisp glyph edges.
        s=(font or self.font).render(str(value),True,color)
        self.canvas.blit(s,(round(x*SCALE),round(y*SCALE)))

    def draw_rect(self,color,rect,width=0,border_radius=0):
        x,y,w,h=rect
        scaled=(round(x*SCALE),round(y*SCALE),round(w*SCALE),round(h*SCALE))
        pygame.draw.rect(self.canvas,color,scaled,width=width*SCALE,
                         border_radius=border_radius*SCALE)

    def draw_line(self,color,start,end,width=1):
        a=(round(start[0]*SCALE),round(start[1]*SCALE))
        b=(round(end[0]*SCALE),round(end[1]*SCALE))
        pygame.draw.line(self.canvas,color,a,b,width*SCALE)
    def clip(self,s,n):
        s=str(s); return s if len(s)<=n else s[:max(0,n-1)]+"."

    def draw_controls(self):
        self.canvas.fill(BLACK)
        self.text(12,27,"KOLIBRIX",GREEN,self.big)
        self.text(12,55,"Choose control method")
        self.text(12,82,"How do you want to control the desktop?")
        options=[("Mouse","Click icons and buttons"),("Keyboard","Use LEFT / RIGHT arrows")]
        for i,(name,description) in enumerate(options):
            x=18+i*146
            selected=i==self.control_choice
            self.draw_rect((35,75,65) if selected else (35,35,35),(x,112,132,58),border_radius=5)
            self.draw_rect(GREEN if selected else GRAY,(x,112,132,58),1,border_radius=5)
            self.text(x+10,122,("> " if selected else "  ")+name,GREEN if selected else WHITE)
            self.text(x+8,145,description,WHITE)
        self.text(12,190,"LEFT / RIGHT: choose   ENTER: confirm",YELLOW)
        self.text(12,220,"KolibriX PC Emulator",GRAY)

    def login(self):
        self.canvas.fill(BLACK); self.text(12,27,"KOLIBRIX",GREEN,self.big)
        self.text(12,48,"CardputerOS login")
        self.text(12,86,"> login: root",GREEN)
        self.text(12,112,self.login_error or "Password: not required",YELLOW)
        self.text(12,160,"User: root")
        self.text(12,173,"Press Enter or click to continue")
        self.text(12,220,"KolibriX PC Emulator",GRAY)

    def status(self):
        self.draw_rect(DGRAY,(0,0,W,STATUS_H))
        self.text(4,3,"KolibriX  "+(self.user or "locked"))
        right="PC SIM  "+time.strftime("%H:%M")
        self.text(W-len(right)*6-4,3,right,CYAN)

    def desktop(self):
        # Windows 11-inspired blue wallpaper and centered taskbar.
        for y in range(H):
            t=y/max(1,H-1)
            col=(int(12+18*t),int(38+55*t),int(90+95*t))
            self.draw_line(col,(0,y),(W,y))
        pygame.draw.ellipse(self.canvas,(22,105,190),(105*SCALE,34*SCALE,220*SCALE,160*SCALE))
        pygame.draw.ellipse(self.canvas,(15,65,145),(145*SCALE,60*SCALE,180*SCALE,120*SCALE))
        for i,(name,color) in enumerate(self.apps):
            col,row=i//3,i%3
            x=10+col*62; y=23+row*62
            self.draw_rect((30,83,155),(x,y,34,34),border_radius=8)
            self.draw_rect(color,(x+4,y+4,26,26),border_radius=6)
            self.text(x+12,y+12,name[0].upper())
            if i==self.selection: self.draw_rect((180,220,255),(x-2,y-2,38,38),1,border_radius=9)
            self.text(x-4,y+37,self.clip(name,10))
        y=H-TASKBAR_H
        self.draw_rect((22,34,58),(34,y+2,252,19),border_radius=8)
        self.draw_rect((65,100,155),(34,y+2,252,19),1,border_radius=8)
        self.draw_rect((55,135,230),(82,y+5,15,13),border_radius=3)
        self.text(87,y+7,"K")
        for j,(glyph,color) in enumerate([("S",(75,145,245)),("F",(50,185,145)),("B",(105,135,245))]):
            xx=112+j*27
            self.draw_rect(color,(xx,y+5,15,13),border_radius=4)
            self.text(xx+5,y+7,glyph)
        self.text(205,y+7,"WiFi")

    def window_frame(self,title):
        self.canvas.fill(DESK)
        self.draw_rect(TITLE_ON,(0,STATUS_H,W,12))
        self.text(4,STATUS_H+2,title)
        self.draw_rect(BLACK,(0,STATUS_H+12,W,H-STATUS_H-12))
        self.draw_rect(WHITE,(0,STATUS_H,W,H-STATUS_H-1),1)

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
            if sel: self.draw_rect(WHITE,(1,yy-1,W-2,9))
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
            if selected: self.draw_rect(WHITE,(2,y-2,W-4,12))
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
        screens={"controls":self.draw_controls,"login":self.login,"desktop":self.desktop,"files":self.draw_files,
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
        if self.screen=="controls":
            if e.key in (pygame.K_LEFT,pygame.K_a): self.control_choice=0
            elif e.key in (pygame.K_RIGHT,pygame.K_d): self.control_choice=1
            elif e.key in (pygame.K_RETURN,pygame.K_SPACE):
                self.control_mode="mouse" if self.control_choice==0 else "keyboard"
                self.screen="login"
            return
        if self.screen=="login":
            if e.key==pygame.K_RETURN:
                self.user="root"; self.password=""
                self.screen="desktop"; self.login_error=""; self.toast="Welcome, root"
            return
        if e.key==pygame.K_ESCAPE or (e.key==pygame.K_w and e.mod&pygame.KMOD_CTRL):
            self.close_top(); return
        if e.key==pygame.K_TAB and e.mod&pygame.KMOD_ALT:
            self.selection=(self.selection+1)%len(self.apps); return
        if e.key==pygame.K_t and e.mod&pygame.KMOD_CTRL and e.mod&pygame.KMOD_ALT:
            self.screen="terminal"; return
        if self.screen=="desktop":
            if self.control_mode=="keyboard":
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
        if self.screen=="controls":
            if 112 <= y <= 170:
                self.control_choice=0 if x<160 else 1
                self.control_mode="mouse" if self.control_choice==0 else "keyboard"
                self.screen="login"
            return
        if self.screen=="login":
            self.user="root"; self.password=""
            self.screen="desktop"; self.login_error=""; self.toast="Welcome, root"
            return
        if self.screen=="desktop" and self.control_mode=="mouse" and STATUS_H<y<H-TASKBAR_H:
            index=min(5,(4 if y>=114 else 0)+min(3,x//80))
            self.selection=index; self.open_app(self.apps[index][0])
        elif self.screen!="login" and x>W-16 and y<STATUS_H+14: self.close_top()

    def run(self):
        while self.running:
            for e in pygame.event.get():
                if e.type==pygame.QUIT: self.running=False
                elif e.type==pygame.KEYDOWN: self.keydown(e)
                elif e.type==pygame.MOUSEBUTTONDOWN and e.button==1:
                    vw,vh=self.viewport()
                    ox=(self.window.get_width()-vw)//2
                    oy=(self.window.get_height()-vh)//2
                    mx,my=e.pos[0]-ox,e.pos[1]-oy
                    if 0 <= mx < vw and 0 <= my < vh:
                        self.mouse_click((mx*W//vw,my*H//vh))
            self.draw()
            self.window.fill(BLACK)
            vw,vh=self.viewport()
            scaled=pygame.transform.smoothscale(self.canvas,(vw,vh))
            self.window.blit(scaled,((self.window.get_width()-vw)//2,
                                     (self.window.get_height()-vh)//2))
            pygame.display.flip(); self.clock.tick(FPS)
        pygame.quit()

    def viewport(self):
        ww,wh=self.window.get_size()
        scale=min(ww/(W*SCALE),wh/(H*SCALE))
        return max(1,round(W*SCALE*scale)),max(1,round(H*SCALE*scale))

if __name__=="__main__":
    try: KolibriXEmulator().run()
    except Exception as exc:
        print("KolibriX Emulator error:",exc,file=sys.stderr); raise
