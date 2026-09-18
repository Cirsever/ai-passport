#!/usr/bin/env python3
"""Render the v2 design specification, not firmware screenshots.

Requires Pillow and the repository's existing CJK source font. Outputs only
original design diagrams; the sample animals are not Codex product assets.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets/images/passport-ui-v2"
FONT = ROOT / "fonts/source/AlibabaPuHuiTi-Regular.ttf"
C = dict(ink="#293E42", muted="#5E7777", paper="#FFF9EB", sky="#DDEDE9",
         blue="#74B6C4", green="#749F83", pale="#E5EDDC", gold="#E2B964",
         amber="#916621", red="#B65F52", pink="#F6E1D7", white="#FFFFFF",
         line="#B8CAC1", shadow="#C5D2C8", bg="#F4F4ED")
FONTS: dict[int, ImageFont.FreeTypeFont] = {}
LANG = "zh"
SCREENS: dict[str, Image.Image] = {}


def font(size: int) -> ImageFont.FreeTypeFont:
    if size not in FONTS:
        FONTS[size] = ImageFont.truetype(str(FONT), size)
    return FONTS[size]


def tr(zh: str, en: str) -> str:
    return zh if LANG == "zh" else en


class Canvas:
    def __init__(self, w=240, h=320, bg="paper"):
        self.im = Image.new("RGB", (w, h), C.get(bg, bg))
        self.d = ImageDraw.Draw(self.im)

    def rect(self, x, y, w, h, fill):
        assert x >= 0 and y >= 0 and x + w <= self.im.width and y + h <= self.im.height
        self.d.rectangle((x, y, x+w-1, y+h-1), fill=C.get(fill, fill))

    def text(self, x, y, text, size=16, color="ink", width=None, center=False):
        if width is not None:
            while self.d.textlength(text, font=font(size)) > width:
                text = text[:-2] + "…"
        length = self.d.textlength(text, font=font(size))
        if center:
            x -= length / 2
        assert x >= 0 and x + length <= self.im.width + 1, (text, x, length)
        bounds = self.d.textbbox((round(x), y), text, font=font(size))
        assert bounds[1] >= 0 and bounds[3] <= self.im.height, (text, bounds)
        self.d.text((round(x), y), text, font=font(size), fill=C.get(color, color))

    def panel(self, x, y, w, h, fill="white", edge="line", shadow=False):
        if shadow:
            self.rect(x+3, y+3, w, h, "shadow")
        self.rect(x, y, w, h, edge)
        self.rect(x+2, y+2, w-4, h-4, fill)
        for a, b in [(x, y), (x+w-2, y), (x, y+h-2), (x+w-2, y+h-2)]:
            self.rect(a, b, 2, 2, "paper")


def battery(s, x=191, y=15, soc=76, charging=False):
    s.rect(x, y, 32, 15, "ink")
    s.rect(x+2, y+2, 28, 11, "paper")
    s.rect(x+32, y+4, 3, 7, "ink")
    if soc < 0:
        s.text(x+16, y-2, "?", 14, center=True)
    else:
        level = (soc+24)//25 if soc else 0
        color = "red" if soc <= 10 else "gold" if soc <= 20 else "green"
        for i in range(4):
            s.rect(x+4+i*6, y+4, 4, 7, color if i < level else "line")
    if charging:
        s.d.polygon([(x+16,y-3),(x+10,y+6),(x+16,y+6),
                     (x+13,y+17),(x+22,y+5),(x+17,y+5)], fill=C["blue"])


def header(s, label="CODEX / 02", soc=76, online=True):
    s.rect(0, 0, 240, 44, "sky")
    s.rect(12, 18, 6, 6, "green" if online else "red")
    s.text(25, 11, label, 14, width=151)
    battery(s, soc=soc)
    s.rect(12, 43, 216, 1, "line")


def footer(s, labels=None, hint=None):
    labels = labels or [(tr("任务", "Task"), "up"),
                        (tr("卡组", "Cards"), "down"), (tr("说话", "Talk"), "ok")]
    s.rect(0, 264, 240, 56, "paper")
    s.rect(12, 264, 216, 1, "line")
    for i, (label, key) in enumerate(labels):
        x = 12 + i*76
        s.panel(x, 273, 68, 24, "pale" if key == "ok" else "white")
        if key in ("up", "down"):
            y = 282
            pts = [(x+8,y+4),(x+12,y),(x+16,y+4)] if key == "up" else [
                (x+8,y),(x+12,y+4),(x+16,y)]
            s.d.polygon(pts, fill=C["ink"])
        else:
            s.rect(x+8, 281, 7, 7, "ink")
        s.text(x+23, 276, label, 12, width=42)
    s.text(120, 302, hint or tr("长按上：会话  ·  长按确认：说话",
                              "Hold UP: sessions · OK: talk"),
           11, "muted", center=True)


def star(s, x, y, color="gold"):
    s.rect(x+3, y, 3, 9, color)
    s.rect(x, y+3, 9, 3, color)


def robot(s, x, y, scale=4, mood="idle"):
    rows = ["....II....", "....II....", "..IIIIII..", ".IIIIIIII.",
            "IIIIIIIIII", "IIWWWWWWII", "IIWBWWBWII", "IIWBWWBWII",
            ".IWWWWWWI.", "..IIIIII..", "...IGGI...", "..IIIIII..",
            "..II..II..", "..II..II..", ".III..III."]
    colors = {"I":"ink", "W":"paper", "B":"blue", "G":"gold"}
    if mood == "sleep":
        rows[6] = "IIWWWWWWII"
        rows[7] = "IIWBWWBWII"
    for row, line in enumerate(rows):
        for col, pixel in enumerate(line):
            if pixel != ".":
                s.rect(x+col*scale, y+row*scale, scale, scale, colors[pixel])


def animal(s, x, y, scale=4, fox=False):
    rows = ["..II......II..", "..IOI....IOI..", "..IOOI..IOOI..",
            "..IOOOOOOOOI..", ".IOOOOOOOOOOI.", ".IOWIOOOIWOOI.",
            ".IOWIOOOIWOOI.", ".IOOOOWOOOOOI.", "..IWWWWWWWWI..",
            "...IWWIIWWI...", "...IOOOOOOI...", "..IOOOOOOOOI..",
            "..IOOOOOOOOI.I", "..IWWWWWWWWII.",
            "...II....II...", "..III....III.."]
    colors = {"I":"ink", "O":"gold" if fox else "blue", "W":"paper"}
    for row, line in enumerate(rows):
        for col, pixel in enumerate(line):
            if pixel != ".":
                s.rect(x+col*scale, y+row*scale, scale, scale, colors[pixel])


def document(s, x, y, scale=1, execute=False):
    s.panel(x, y, 36*scale, 44*scale, "paper", "ink", True)
    if execute:
        s.text(x+18*scale, y+9*scale, ">_", 18*scale, center=True)
    else:
        s.rect(x+8*scale, y+10*scale, 14*scale, 3*scale, "blue")
        for yy in [20, 27, 34]:
            s.rect(x+8*scale, y+yy*scale, 20*scale, 2*scale, "line")


def landscape(s, y=83, h=119):
    s.rect(12, y, 216, h, "sky")
    for x, yy in [(25, y+18), (178, y+31)]:
        s.rect(x, yy, 29, 5, "white")
        s.rect(x+6, yy-5, 15, 5, "white")
    s.rect(26, y+h-10, 188, 6, "green")
    s.rect(30, y+h-4, 180, 3, "shadow")
    for x in [40, 57, 186, 202]:
        s.rect(x, y+h-14, 3, 4, "green")


def title(s, primary, secondary=None):
    s.text(16, 51, primary, 18, width=208)
    if secondary:
        s.text(16, 76, secondary, 12, "muted", width=208)


def card(s, n=1, scanning=False, pet=False, fox=False):
    for i in reversed(range(n)):
        x, y = 30+i*12, 108-i*9
        s.panel(x, y, 143, 88, "paper", "ink" if i == 0 else "muted", True)
        s.rect(x+4, y+4, 135, 6, ["blue","gold","green","red"][i])
        s.rect(x+128, y+17, 11, 18, "gold" if i == 0 else "line")
        if i == 0:
            if pet:
                animal(s, x+14, y+18, 3, fox)
            else:
                robot(s, x+19, y+20, 3)
            s.text(x+68, y+23, "P-02", 14)
            s.rect(x+68, y+45, 37, 3, "line")
            for b in range(3):
                s.rect(x+68+b*10, y+60, 6, 6, "blue")
            if scanning:
                s.rect(x+7, y+56, 115, 2, "blue")
                s.rect(x+7, y+58, 115, 2, "sky")


def base(key, label="CODEX / 02", soc=76, online=True):
    s = Canvas()
    header(s, label, soc, online)
    SCREENS[key] = s.im
    return s


def render_screens():
    s=base("01-home", tr("随身伙伴", "Passport"))
    title(s, tr("今天，一起做点什么", "A little company"))
    landscape(s)
    robot(s, 94, 119, 5)
    star(s, 63, 116)
    s.text(120, 213, tr("贴卡，唤醒你的伙伴", "Tap a card to begin"), 16, center=True)
    s.text(120, 239, tr("NFC 读卡区", "NFC reader"), 12, "muted", center=True)
    footer(s)

    for key, count, scanning, text in [
        ("02-scan",1,True,tr("正在读取卡片", "Reading card")),
        ("03-ready",1,False,tr("伙伴已就绪", "Ready to work")),
        ("04-stack",3,False,tr("3 张卡片，一起协作", "3 cards, one context")),
    ]:
        s=base(key)
        title(s, tr("我的卡牌", "My cards") if count==1 else tr("我的卡组", "My stack"))
        landscape(s)
        card(s,count,scanning)
        s.text(120,213,text,17,center=True)
        s.text(120,239,tr("等待 IDE 回应","Waiting for IDE") if scanning else
               tr("Passport · 界面优化","Passport · UI polish"),12,"muted",center=True)
        footer(s, [(tr("返回","Back"),"up"), (tr("选卡","Select"),"down"),
                   (tr("说话","Talk"),"ok")] if count > 1 else None)

    s=base("05-task")
    title(s,tr("界面优化","UI polish"),tr("Passport · 当前任务","Passport · Active task"))
    robot(s,29,110,4)
    s.text(104,122,tr("正在实现","Implementing"),16)
    s.text(104,149,"42%",24)
    s.rect(18,184,204,8,"line"); s.rect(18,184,86,8,"green")
    s.text(18,207,tr("12:30  布局已更新","12:30  Layout updated"),14)
    s.text(18,233,tr("12:31  正在检查边界","12:31  Checking bounds"),14)
    footer(s,[(tr("返回","Back"),"up"),(tr("翻页","More"),"down"),(tr("已阅","Read"),"ok")])

    s=base("06-offline",online=False)
    title(s,tr("连接暂时断开","Connection lost"))
    landscape(s); robot(s,94,119,5,"sleep")
    s.text(120,213,tr("伙伴还在这里","Your buddy is here"),17,center=True)
    s.text(120,239,tr("保留上次状态 · 自动重连","Last snapshot · reconnecting"),12,"muted",center=True)
    footer(s,hint=tr("连接恢复后再继续操作","Actions resume after reconnect"))

    s=base("07-approval")
    title(s,tr("允许修改？","Allow changes?"),tr("Passport · 界面优化","Passport · UI polish"))
    robot(s,54,106,4); document(s,126,103,1)
    star(s,175,109,"gold")
    s.panel(16,174,208,74,"white")
    s.text(28,184,tr("修改 3 个文件","Edit 3 files"),18)
    s.text(28,216,tr("当前项目","This project"),13,"muted")
    s.text(182,216,"48s",13,"amber")
    footer(s,[(tr("拒绝","Deny"),"up"),(tr("详情","Info"),"down"),(tr("允许","Allow"),"ok")],
           tr("仅允许本次修改","Allow this change only"))

    s=base("08-approval-details")
    title(s,tr("将修改这些文件","Files to change"),tr("Passport · 界面优化","Passport · UI polish"))
    for i,(name,change) in enumerate([("main/ui.c","+12 -4"),("main/ui.h","+3 -1"),("tests/ui.c","+8 -0")]):
        y=104+i*43
        s.rect(18,y+6,11,15,"blue")
        s.text(39,y,name,14,width=130)
        s.text(39,y+20,change,12,"muted")
        s.rect(18,y+40,204,1,"line")
    s.text(120,239,tr("范围 1/1 · 48s","Scope 1/1 · 48s"),12,"muted",center=True)
    footer(s,[(tr("返回","Back"),"up"),(tr("翻页","More"),"down"),(tr("允许","Allow"),"ok")],
           tr("允许前可返回操作卡","Return to the card to deny"))

    for key,label,sub,color in [
        ("09-allowed",tr("已允许","Allowed"),tr("IDE 已收到本次决定","IDE received your decision"),"green"),
        ("10-denied",tr("已拒绝","Denied"),tr("IDE 已取消本次操作","IDE cancelled this action"),"red"),
        ("11-expired",tr("这次请求已过期","Request expired"),tr("没有执行此次修改","This change was not executed"),"muted"),
    ]:
        s=base(key); title(s,label)
        landscape(s)
        robot(s,61,124,4)
        s.panel(124,122,58,48,"paper",color)
        if key=="09-allowed":
            s.d.line([(135,146),(145,156),(172,131)],fill=C[color],width=5)
        elif key=="10-denied":
            s.d.line([(140,134),(166,159)],fill=C[color],width=4)
            s.d.line([(166,134),(140,159)],fill=C[color],width=4)
        else:
            s.text(153,130,"--",22,color,center=True)
        s.text(120,219,sub,14,center=True)
        s.text(120,241,tr("稍后返回当前会话","Returning to your session"),11,"muted",center=True)
        footer(s,hint=tr("结果来自 IDE 回执","Result confirmed by IDE"))

    s=base("12-voice")
    title(s,tr("我在听","Listening"),tr("只发给 CODEX / 02","Send only to CODEX / 02"))
    robot(s,98,108,4)
    for i,h in enumerate([9,16,29,18,40,25,15,31,12,20,8]):
        s.rect(35+i*16,208-h,7,h,"blue")
    s.text(120,223,"00:04",20,center=True)
    footer(s,[(tr("锁定","Locked"),"up"),(tr("锁定","Locked"),"down"),(tr("录音","Record"),"ok")],
           tr("松开确认键，立即停止","Release OK to stop"))

    s=base("13-sessions")
    title(s,tr("和谁继续？","Continue with…"),tr("当前 CODEX / 02","Current CODEX / 02"))
    entries=[("01",tr("修复串口","Fix serial"),tr("运行中","Running")),
             ("02",tr("界面优化","UI polish"),tr("当前","Current")),
             ("03",tr("文档整理","Edit docs"),tr("待确认","Needs review"))]
    for i,(num,name,state) in enumerate(entries):
        y=103+i*47
        s.panel(16,y,208,42,"pale" if i==2 else "white","ink" if i==2 else "line")
        robot(s,24,y+7,2)
        s.text(54,y+4,num+"  "+name,14,width=159)
        s.text(54,y+24,"CODEX · "+state,11,"amber" if i==2 else "muted",width=159)
    s.text(120,246,tr("3 个会话 · 选择 03","3 sessions · select 03"),11,"muted",center=True)
    footer(s,[(tr("上一个","Prev"),"up"),(tr("下一个","Next"),"down"),(tr("切换","Switch"),"ok")],
           tr("长按上：取消并返回","Hold UP to cancel"))

    s=base("14-switching")
    title(s,tr("正在切换","Switching"))
    robot(s,41,113,4); robot(s,157,113,4)
    s.text(61,188,"02",18,center=True); s.text(177,188,"03",18,center=True)
    for x in [101,112,123]: s.rect(x,141,5,5,"blue")
    s.text(120,223,tr("等待主机确认","Waiting for host"),16,center=True)
    footer(s,[(tr("取消","Cancel"),"up"),(tr("稍候","Wait"),"down"),(tr("稍候","Wait"),"ok")],
           tr("确认前不向新会话发送操作","No actions until confirmed"))

    s=base("15-switched",label="CODEX / 03")
    title(s,tr("文档整理","Edit docs"))
    landscape(s); robot(s,94,119,5)
    s.text(120,214,tr("已切换到会话 03","Now in session 03"),17,center=True)
    s.text(120,239,tr("Passport · 待确认 1","Passport · 1 pending request"),12,"amber",center=True)
    footer(s)

    s=base("16-unavailable")
    title(s,tr("会话已结束","Session ended"))
    landscape(s); robot(s,94,119,5,"sleep")
    s.text(120,212,tr("仍在会话 02","Still in session 02"),17,center=True)
    s.text(120,239,tr("重新选择一个会话","Choose another session"),13,"muted",center=True)
    footer(s,[(tr("返回","Back"),"up"),(tr("会话","List"),"down"),(tr("重试","Retry"),"ok")])

    for key,done in [("17-pet-sync",False),("18-pet-synced",True)]:
        s=base(key)
        title(s,tr("伙伴换装","A familiar friend"),tr("跟随 PC 端伙伴","Follows your desktop pet"))
        landscape(s,97,112)
        card(s,pet=True,fox=done)
        if not done:
            for x in [99,110,121]: s.rect(x,217,5,5,"blue")
        s.text(120,230,tr("新伙伴正在路上","New look on its way") if not done else
               tr("还是你的那位伙伴","Same buddy, new pixels"),14,center=True)
        footer(s)

    s=base("19-low-battery",soc=8)
    title(s,tr("伙伴需要充电了","Battery running low"))
    landscape(s); robot(s,94,119,5,"sleep")
    s.text(120,213,tr("电量较低","Low battery"),17,"red",center=True)
    s.text(120,239,tr("当前会话保留","Your session is preserved"),13,"muted",center=True)
    footer(s)

    s=base("20-battery-unknown",soc=-1)
    title(s,tr("我的卡牌","My card"))
    landscape(s); card(s)
    s.text(120,213,tr("伙伴已就绪","Ready to work"),17,center=True)
    s.text(120,239,tr("电量暂不可用","Battery reading unavailable"),12,"muted",center=True)
    footer(s)

    s=base("21-readonly",label="TRAE / --")
    title(s,tr("在电脑上继续","Continue on desktop"),tr("当前 IDE 仅支持查看","This IDE supports viewing only"))
    robot(s,91,111,5)
    s.panel(20,210,200,38,"sky")
    s.text(120,219,tr("请在 IDE 选择会话","Select a session in IDE"),14,center=True)
    footer(s,[(tr("返回","Back"),"up"),(tr("查看","View"),"down"),(tr("只读","View"),"ok")],
           tr("无法确定目标时暂停发送","Sending paused: no exact target"))

    s=base("22-voice-stopped")
    title(s,tr("录音已停止","Recording stopped"),tr("目标 CODEX / 02","Target CODEX / 02"))
    robot(s,98,108,4)
    for x in range(35,205,16): s.rect(x,204,7,4,"green")
    s.text(120,224,tr("正在发送语音","Sending voice"),16,center=True)
    footer(s,[(tr("稍候","Wait"),"up"),(tr("稍候","Wait"),"down"),(tr("已停止","Stopped"),"ok")],
           tr("2 秒后恢复按键提示","Key hints return after 2 seconds"))

    s=base("23-pet-fallback")
    title(s,tr("伙伴仍在这里","Still here with you"),tr("保留上次形象","Keeping the previous look"))
    landscape(s,97,112); card(s,pet=True)
    s.text(120,219,tr("同步稍后重试","Will retry sync later"),15,center=True)
    s.text(120,242,tr("不影响当前任务","Your task continues"),12,"muted",center=True)
    footer(s)

    s=base("24-no-sessions",label="CODEX / --")
    title(s,tr("还没有可用会话","No sessions yet"))
    landscape(s); robot(s,94,119,5,"sleep")
    s.text(120,213,tr("先在 IDE 开始对话","Start a chat in your IDE"),16,center=True)
    s.text(120,239,tr("发现会话后自动更新","List updates when available"),12,"muted",center=True)
    footer(s,[(tr("返回","Back"),"up"),(tr("刷新","Refresh"),"down"),(tr("查看","View"),"ok")])


CAPTIONS = [
    ("空闲首页","Idle home"),("单卡扫描","Card scan"),("单卡就绪","Card ready"),
    ("多卡叠放","Card stack"),("任务进展","Task progress"),("断线保留","Offline snapshot"),
    ("伙伴递来操作卡","Action request"),("展开修改范围","Change details"),("允许已确认","Allow acknowledged"),
    ("拒绝已确认","Deny acknowledged"),("请求过期","Request expired"),("录音锁定会话","Voice target locked"),
    ("会话选择器","Session picker"),("等待切换回执","Await switch ACK"),("切换成功","Switch confirmed"),
    ("目标会话结束","Target unavailable"),("P1 同步中","P1 syncing"),("P1 像素伙伴","P1 synced pet"),
    ("低电量","Low battery"),("电量未知","Battery unavailable"),("适配器只读","Adapter read-only"),
    ("松键停止录音","Voice stopped"),("P1 同步失败回退","P1 sync fallback"),("没有可用会话","No sessions"),
]


def sheet(group, names):
    s=Canvas(1568,1604,"bg")
    headings=[("日常陪伴与卡牌","Everyday scenes"),
              ("操作确认与语音","Approval and voice"),
              ("多会话与伙伴同步","Sessions and companion sync"),
              ("异常与降级状态","Fallback states")]
    s.text(40,24,"PASSPORT / PIXEL UI V2",18,"muted")
    s.text(40,59,tr(*headings[group]),32)
    s.text(40,105,tr("240 × 320 · 设计预览，非固件截图","240 × 320 · Design preview, not firmware capture"),18,"muted")
    for i,key in enumerate(names):
        x=40+(i%3)*512; y=157+(i//3)*716
        s.im.paste(SCREENS[key].resize((480,640),Image.Resampling.NEAREST),(x,y))
        index=int(key[:2])-1
        s.text(x,y+652,f"{index+1:02d} / "+tr(*CAPTIONS[index]),22)
    s.im.save(OUT/f"{group+1:02d}-overview.{LANG}.png",optimize=True)


def battery_sheet():
    s=Canvas(1200,232,"bg")
    s.text(28,18,tr("像素电池 · 固定在顶栏右侧","Pixel battery · fixed at top right"),24)
    cases=[(100,False),(55,False),(18,False),(8,False),(0,False),(-1,False),(55,True)]
    labels=[tr("满电","Full"),tr("普通","Normal"),tr("偏低","Low"),tr("很低","Critical"),
            tr("空电","Empty"),tr("未知","Unknown"),tr("充电*","Charging*")]
    for i,(soc,charging) in enumerate(cases):
        small=Canvas(48,32)
        battery(small,5,8,soc,charging)
        s.im.paste(small.im.resize((144,96),Image.Resampling.NEAREST),(24+i*166,65))
        s.text(96+i*166,165,labels[i],17,center=True)
    s.text(28,202,tr("* 充电状态待硬件提供可信信号；P0 不显示闪电",
                    "* Charging requires a verified signal; no bolt in P0"),15,"muted")
    s.im.save(OUT/f"battery-states.{LANG}.png",optimize=True)


def sync_sheet():
    s=Canvas(1200,420,"bg")
    s.text(28,18,tr("P1 / PC 换伙伴，卡牌自动跟随","P1 / Desktop pet to pixel card"),24)
    steps=[tr("PC 当前伙伴","Desktop selection"),tr("主机转换并校验","Host conversion"),
           tr("设备校验后替换","Device atomic swap")]
    for i in range(3):
        x=28+i*397
        s.panel(x,76,350,274,"white","line")
        s.text(x+175,94,steps[i],21,center=True)
    # An original smooth source illustrates real local reduction/quantization.
    original=Image.new("RGB",(128,128),C["white"])
    d=ImageDraw.Draw(original)
    d.line([(93,99),(110,97),(115,85)],fill=C["ink"],width=9)
    d.line([(93,99),(109,96),(113,85)],fill=C["gold"],width=4)
    d.rounded_rectangle((36,61,95,115),radius=19,fill=C["gold"],outline=C["ink"],width=4)
    d.ellipse((45,73,85,110),fill=C["paper"])
    d.polygon([(25,48),(28,9),(49,30)],fill=C["ink"])
    d.polygon([(80,30),(101,9),(105,48)],fill=C["ink"])
    d.polygon([(31,37),(33,21),(44,34)],fill=C["gold"])
    d.polygon([(86,34),(96,21),(99,37)],fill=C["gold"])
    d.rounded_rectangle((23,28,106,80),radius=21,fill=C["gold"],outline=C["ink"],width=4)
    d.ellipse((37,43,55,64),fill=C["paper"])
    d.ellipse((75,43,93,64),fill=C["paper"])
    d.ellipse((43,44,51,61),fill=C["ink"])
    d.ellipse((78,44,86,61),fill=C["ink"])
    d.polygon([(58,63),(70,63),(64,70)],fill=C["ink"])
    d.rounded_rectangle((36,107,55,121),radius=4,fill=C["ink"])
    d.rounded_rectangle((78,107,97,121),radius=4,fill=C["ink"])
    pixel=original.resize((32,32),Image.Resampling.BOX).quantize(colors=16).convert("RGB")
    s.im.paste(original,(139,133))
    s.im.paste(pixel.resize((96,96),Image.Resampling.NEAREST),(552,139))
    s.text(600,248,"32 × 32 / 16 colors",18,center=True)
    s.text(600,287,tr("保留轮廓、配色、标志特征","Keep silhouette and markings"),16,"muted",center=True)
    s.panel(891,145,172,120,"paper","ink",True)
    s.im.paste(pixel.resize((64,64),Image.Resampling.NEAREST),(906,168))
    s.text(988,180,"P-02",18)
    s.text(990,287,tr("任务和会话不变","Session and task preserved"),16,"muted",center=True)
    s.text(28,380,tr("示例动物为原创示意，不代表已接入 Codex 宠物资源",
                    "Original example animal; not a verified Codex pet asset"),16,"muted")
    s.im.save(OUT/f"pet-sync-flow.{LANG}.png",optimize=True)


def main():
    global LANG
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample",action="store_true",help="Render only approval sample")
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    for LANG in ("zh","en"):
        SCREENS.clear()
        render_screens()
        if args.sample:
            sample_dir = ROOT / "build/passport-design-samples"
            sample_dir.mkdir(parents=True, exist_ok=True)
            SCREENS["07-approval"].resize((720,960),Image.Resampling.NEAREST).save(
                sample_dir/f"approval-sample.{LANG}.png")
            continue
        for key,im in SCREENS.items():
            im.save(OUT/f"{key}.{LANG}.png",optimize=True)
        keys=list(SCREENS)
        for group in range(4):
            sheet(group,keys[group*6:group*6+6])
        battery_sheet()
        sync_sheet()
    if not args.sample:
        manifest={"size":[240,320],"states":list(SCREENS),"languages":["zh","en"],
                  "status":"Design proposal, not firmware capture",
                  "generator":"tools/render_passport_design.py"}
        (OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("Design preview:", "sample" if args.sample else "24 states × 2 languages; 12 boards")


if __name__=="__main__":
    main()
