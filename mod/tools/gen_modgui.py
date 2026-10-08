#!/usr/bin/env python3
"""Generate the KR-106 modgui (MOD / mod-ui) from the port table.

    python3 tools/gen_modgui.py build/ports.json modgui/

ports.json is written by kr106_mod_ttlgen (see the Makefile `modgui` target).
Writes into modgui/:
  icon-kr106.html, stylesheet-kr106.css, script-kr106.js and the film-strip
  images (faders, switches, buttons). Everything is drawn here at 2x with 4x
  supersampling, in the style of the Roland Juno-60/106 front panel.

The generated files are committed, so building the plugin needs neither
Python nor Pillow. Requires: Pillow.
"""

import json
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

DPR = 2            # image pixels per CSS pixel
SS = 4             # supersampling factor while drawing
K = DPR * SS       # drawing pixels per CSS pixel


# ----------------------------------------------------------------------------
# Drawing helpers
# ----------------------------------------------------------------------------

def rgba(hex_color, a=255):
    h = hex_color.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def lerp_color(c1, c2, t):
    return tuple(int(round(a + (b - a) * t)) for a, b in zip(c1, c2))


def new_layer(w, h):
    return Image.new('RGBA', (int(w * K), int(h * K)), (0, 0, 0, 0))


def finish(img):
    """Downsample a supersampled layer to DPR resolution."""
    w, h = img.size
    return img.resize((w // SS, h // SS), Image.LANCZOS)


def box(x0, y0, x1, y1):
    return [x0 * K, y0 * K, x1 * K - 1, y1 * K - 1]


def vgradient_rrect(img, x0, y0, x1, y1, radius, top, bottom, outline=None, owidth=1.0):
    """Rounded rectangle filled with a vertical gradient (CSS px coordinates)."""
    w, h = int((x1 - x0) * K), int((y1 - y0) * K)
    grad = Image.new('RGBA', (w, h))
    gd = ImageDraw.Draw(grad)
    for y in range(h):
        gd.line([(0, y), (w, y)], fill=lerp_color(top, bottom, y / max(1, h - 1)))
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius=radius * K, fill=255)
    img.paste(grad, (int(x0 * K), int(y0 * K)), mask)
    if outline:
        ImageDraw.Draw(img).rounded_rectangle(box(x0, y0, x1, y1), radius=radius * K,
                                              outline=outline, width=max(1, int(owidth * K)))


def soft_shadow(img, x0, y0, x1, y1, radius, offset, blur, alpha):
    sh = new_layer(img.size[0] / K, img.size[1] / K)
    ImageDraw.Draw(sh).rounded_rectangle(box(x0, y0 + offset, x1, y1 + offset), radius=radius * K,
                                         fill=(0, 0, 0, alpha))
    sh = sh.filter(ImageFilter.GaussianBlur(blur * K))
    img.alpha_composite(sh)


def strip(frames):
    """Join frames horizontally into a film strip (frame 0 = minimum value)."""
    w, h = frames[0].size
    out = Image.new('RGBA', (w * len(frames), h), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (i * w, 0))
    return out


def save(img, path):
    img.save(path, optimize=True)


# ----------------------------------------------------------------------------
# Components (sizes in CSS px)
# ----------------------------------------------------------------------------

FADER_W, FADER_H = 30, 116
FADER_TOP, FADER_BOT = 13, 103      # cap centre travel


def fader_static(positions, major):
    """Slot and scale marks shared by every frame."""
    img = new_layer(FADER_W, FADER_H)
    d = ImageDraw.Draw(img)
    cx = FADER_W / 2
    for i, t in enumerate(positions):
        y = FADER_BOT - t * (FADER_BOT - FADER_TOP)
        is_major = i in major
        length = 6 if is_major else 4
        col = rgba('#d8d8d8') if is_major else rgba('#9a9a9a')
        th = 0.9 if is_major else 0.7
        d.rectangle(box(cx - 8 - length, y - th / 2, cx - 8, y + th / 2), fill=col)
        d.rectangle(box(cx + 8, y - th / 2, cx + 8 + length, y + th / 2), fill=col)
    # slot: black groove with a faint lower lip highlight
    d.rounded_rectangle(box(cx - 2.2, 5, cx + 2.2, FADER_H - 5), radius=2.2 * K, fill=rgba('#000000'))
    d.rounded_rectangle(box(cx - 2.2, 5, cx + 2.2, FADER_H - 5), radius=2.2 * K,
                        outline=rgba('#4a4a4c'), width=int(0.5 * K))
    return img


def fader_cap(img, t):
    """Juno slider cap: dark grey block with a white index line."""
    cy = FADER_BOT - t * (FADER_BOT - FADER_TOP)
    x0, x1 = 4, FADER_W - 4
    y0, y1 = cy - 7, cy + 7
    soft_shadow(img, x0, y0, x1, y1, 1.5, 2.0, 1.4, 170)
    vgradient_rrect(img, x0, y0, x1, y1, 1.6, rgba('#58585b'), rgba('#1b1b1d'), outline=rgba('#050505'), owidth=0.6)
    d = ImageDraw.Draw(img)
    # top bevel and bottom edge
    d.rectangle(box(x0 + 1, y0 + 0.8, x1 - 1, y0 + 1.4), fill=rgba('#8a8a8d', 200))
    d.rectangle(box(x0 + 1, y1 - 1.6, x1 - 1, y1 - 0.8), fill=rgba('#0d0d0e', 220))
    # grip ridges
    for dy in (-4.2, 4.2):
        d.rectangle(box(x0 + 2, cy + dy - 0.35, x1 - 2, cy + dy + 0.35), fill=rgba('#121213', 230))
    # index line
    d.rectangle(box(x0 + 1.5, cy - 0.8, x1 - 1.5, cy + 0.8), fill=rgba('#f6f6f2'))


def make_fader(n_frames, positions, major):
    base = fader_static(positions, major)
    frames = []
    for i in range(n_frames):
        f = base.copy()
        fader_cap(f, i / (n_frames - 1))
        frames.append(finish(f))
    return strip(frames)


SW_W, SW_H = 16, 44


def switch_frame(pos, n, horizontal=False):
    """Juno lever switch. pos 0 = top (or left)."""
    w, h = (SW_H, SW_W) if horizontal else (SW_W, SW_H)
    img = new_layer(w, h)
    d = ImageDraw.Draw(img)
    # bezel and slot
    d.rounded_rectangle(box(0.5, 0.5, w - 0.5, h - 0.5), radius=(min(w, h) / 2 - 0.5) * K,
                        fill=rgba('#000000'), outline=rgba('#505052'), width=int(0.6 * K))
    d.rounded_rectangle(box(2.2, 2.2, w - 2.2, h - 2.2), radius=(min(w, h) / 2 - 2.2) * K, fill=rgba('#0b0b0c'))

    # lever: a white tip fading into the dark slot towards the pivot (centre)
    long_side = SW_H
    span = long_side - 4.4
    third = span / 3
    lever = new_layer(w, h)
    ld = ImageDraw.Draw(lever)
    if n == 2:
        centres = [2.2 + third * 0.95, long_side - 2.2 - third * 0.95]
    else:
        centres = [2.2 + third * 0.75, long_side / 2, long_side - 2.2 - third * 0.75]
    c = centres[pos]
    middle = (n == 3 and pos == 1)
    if middle:
        r = 4.6
        cx, cy = (c, w / 2) if horizontal else (w / 2, c)
        for k in range(int(r * K), 0, -1):
            t = k / (r * K)
            col = lerp_color(rgba('#ffffff'), rgba('#b9b9b9'), t ** 2)
            ld.ellipse([cx * K - k, cy * K - k, cx * K + k, cy * K + k], fill=col)
    else:
        toward_start = pos == 0
        tip_len = third * 1.55
        t0, t1 = (c - third * 0.55, c - third * 0.55 + tip_len) if toward_start else (c + third * 0.55 - tip_len, c + third * 0.55)
        steps = int((t1 - t0) * K)
        for s in range(steps):
            u = s / max(1, steps - 1)
            a = u if not toward_start else 1 - u    # 1 at the tip
            col = lerp_color(rgba('#2a2a2a'), rgba('#ffffff'), min(1.0, a * 1.35))
            p = t0 * K + s
            if horizontal:
                ld.line([(p, 3.4 * K), (p, (h - 3.4) * K)], fill=col)
            else:
                ld.line([(3.4 * K, p), ((w - 3.4) * K, p)], fill=col)
        mask = Image.new('L', lever.size, 0)
        md = ImageDraw.Draw(mask)
        if horizontal:
            md.rounded_rectangle([t0 * K, 3.4 * K, t1 * K, (h - 3.4) * K], radius=(h / 2 - 3.4) * K, fill=255)
        else:
            md.rounded_rectangle([3.4 * K, t0 * K, (w - 3.4) * K, t1 * K], radius=(w / 2 - 3.4) * K, fill=255)
        empty = new_layer(w, h)
        lever = Image.composite(lever, empty, mask)
    img.alpha_composite(lever)
    return finish(img)


def make_switch(n, horizontal=False):
    return strip([switch_frame(i, n, horizontal) for i in range(n)])


PAD_W, PAD_H = 30, 46
PAD_COLORS = {
    'cream':  ('#f1ead2', '#cfc4a4'),
    'yellow': ('#f6dc4a', '#d2b222'),
    'orange': ('#f79a2c', '#d06f10'),
    'grey':   ('#bdbdbd', '#8c8c8c'),
}


def led(img, cx, cy, r, on):
    if on:
        glow = new_layer(img.size[0] / K, img.size[1] / K)
        ImageDraw.Draw(glow).ellipse([(cx - r * 2.1) * K, (cy - r * 2.1) * K, (cx + r * 2.1) * K, (cy + r * 2.1) * K],
                                     fill=(255, 40, 30, 120))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(r * 0.9 * K)))
    d = ImageDraw.Draw(img)
    d.ellipse(box(cx - r - 0.9, cy - r - 0.9, cx + r + 0.9, cy + r + 0.9), fill=rgba('#050505'))
    inner, outer = (rgba('#ffd2c8'), rgba('#e8160e')) if on else (rgba('#7a2a24'), rgba('#3a0d0b'))
    steps = int(r * K)
    for k in range(steps, 0, -1):
        t = k / steps
        d.ellipse([(cx - 0.25 * r) * K - k * 0.9, (cy - 0.3 * r) * K - k * 0.9,
                   (cx - 0.25 * r) * K + k * 0.9, (cy - 0.3 * r) * K + k * 0.9], fill=lerp_color(inner, outer, t ** 0.7))
    # clip to the LED lens
    lens = new_layer(img.size[0] / K, img.size[1] / K)
    ImageDraw.Draw(lens).ellipse(box(cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255, 255))
    spec = new_layer(img.size[0] / K, img.size[1] / K)
    ImageDraw.Draw(spec).ellipse(box(cx - r * 0.55, cy - r * 0.7, cx - r * 0.05, cy - r * 0.25),
                                 fill=(255, 255, 255, 150 if on else 70))
    img.alpha_composite(spec)


def pad_frame(color, on, with_led=True):
    img = new_layer(PAD_W, PAD_H)
    top, bottom = PAD_COLORS[color]
    if with_led:
        led(img, PAD_W / 2, 6.5, 3.6, on)
    x0, y0, x1, y1 = 1.5, 16, PAD_W - 1.5, PAD_H - 2
    soft_shadow(img, x0, y0, x1, y1, 1.5, 1.6, 1.2, 190)
    # body
    vgradient_rrect(img, x0, y0, x1, y1, 1.6, rgba(top), rgba(bottom), outline=rgba('#121212'), owidth=0.6)
    d = ImageDraw.Draw(img)
    # bevel: bright top edge, darker front lip
    d.rectangle(box(x0 + 1.2, y0 + 0.8, x1 - 1.2, y0 + 1.5), fill=(255, 255, 255, 140))
    lip = lerp_color(rgba(bottom), rgba('#000000'), 0.35)
    d.rounded_rectangle(box(x0 + 0.6, y1 - 3.2, x1 - 0.6, y1 - 0.6), radius=1.2 * K, fill=lip)
    return finish(img)


def make_pad(color, with_led=True):
    return strip([pad_frame(color, False, with_led), pad_frame(color, True, with_led)])


# ----------------------------------------------------------------------------
# Panel layout
# ----------------------------------------------------------------------------

def fader(symbol, label, style='uni'):
    return {'type': 'fader', 'symbol': symbol, 'label': label, 'style': style}


def sw(symbol, label, labels=None):
    return {'type': 'switch', 'symbol': symbol, 'label': label, 'labels': labels}


def pad(symbol, label, color='yellow'):
    return {'type': 'pad', 'symbol': symbol, 'label': label, 'color': color}


def select(symbol, label, width=96):
    return {'type': 'select', 'symbol': symbol, 'label': label, 'width': width}


def hsw(symbol, label, left, right):
    return {'type': 'hswitch', 'symbol': symbol, 'label': label, 'left': left, 'right': right}


CHORUS = {'type': 'chorus'}

# Tabs -> sub-tabs -> sections. Sections are laid out left to right like the
# Juno panel; each sub-tab holds what fits in the panel width.
DCO = ('DCO', 'red', [
    sw('dco_range', 'RANGE', ["16'", "8'", "4'"]),
    fader('dco_lfo', 'LFO'),
    fader('dco_pwm', 'PWM'),
    sw('pwm_mode', ' ', ['LFO', 'MAN', 'ENV']),
    pad('dco_pulse', 'PULSE', 'yellow'),
    pad('dco_saw', 'SAW', 'yellow'),
    pad('dco_sub_on', 'SUB', 'orange'),
    fader('dco_sub', 'SUB'),
    fader('dco_noise', 'NOISE'),
])
HPF = ('HPF', 'red', [fader('hpf', 'FREQ', 'hpf')])
VCF = ('VCF', 'red', [
    fader('vcf_freq', 'FREQ'),
    fader('vcf_res', 'RES'),
    sw('vcf_env_inv', 'ENV', ['NORM', 'INV']),
    fader('vcf_env', ' '),
    fader('vcf_lfo', 'LFO'),
    fader('vcf_kbd', 'KYBD'),
])
VCA = ('VCA', 'red', [
    sw('vca_mode', ' ', ['ENV', 'GATE']),
    fader('vca_level', 'LEVEL'),
])
ENV = ('ENV', 'red', [fader('env_a', 'A'), fader('env_d', 'D'), fader('env_s', 'S'), fader('env_r', 'R')])
CHORUS_SEC = ('CHORUS', 'blue', [CHORUS])
ARP = ('ARPEGGIO', 'blue', [
    pad('arp_on', 'ON/OFF', 'yellow'),
    sw('arp_mode', 'MODE', ['UP', 'U&D', 'DN']),
    sw('arp_range', 'RANGE', ['1', '2', '3']),
    fader('arp_rate', 'RATE'),
    {'type': 'stack', 'items': [pad('arp_sync', 'SYNC', 'cream'), pad('arp_limit', 'KBD LIM', 'cream')]},
    select('arp_div', 'SYNC DIV', 92),
])
LFO = ('LFO', 'red', [
    fader('lfo_rate', 'RATE'),
    fader('lfo_delay', 'DELAY'),
    sw('lfo_mode', 'MODE', ['AUTO', 'MAN']),
    {'type': 'stack', 'items': [pad('lfo_trig', 'TRIG', 'grey'), pad('lfo_sync', 'SYNC', 'cream')]},
    select('lfo_div', 'SYNC DIV', 92),
])
PERF = ('PERFORMANCE', 'red', [
    pad('hold', 'HOLD', 'yellow'),
    sw('porta_mode', 'ASSIGN', ['MONO', 'POLY I', 'POLY II']),
    fader('porta_rate', 'PORTA'),
    fader('bender', 'BEND', 'bi'),
    fader('bend_dco', 'DCO'),
    fader('bend_vcf', 'VCF'),
    fader('bend_lfo', 'LFO'),
])
MASTER = ('MASTER', 'red', [
    fader('volume', 'VOLUME'),
    fader('tuning', 'TUNE', 'bi'),
    select('transpose', 'TRANSPOSE', 70),
])
MODEL = ('MODEL', 'red', [hsw('model', 'ENGINE', '60', '106')])
VOICES = ('VOICES', 'blue', [
    select('voices', 'VOICES', 56),
    select('osc_mode', 'OSCILLATOR', 96),
    select('oversample', 'VCF OVERSAMPLE', 72),
])
KEYBOARD = ('KEYBOARD', 'blue', [
    pad('ignore_vel', 'NO VEL', 'cream'),
    pad('mono_retrig', 'RETRIG', 'cream'),
])
INFO = ('INFO', 'grey', [{'type': 'info'}])

TABS = [
    ('sound', 'VOICE', [
        ('dco', 'DCO · HPF', [DCO, HPF]),
        ('vcf', 'VCF · VCA', [VCF, VCA]),
        ('env', 'ENV · CHORUS', [ENV, CHORUS_SEC]),
    ]),
    ('mod', 'ARP · LFO · PERF', [
        ('arp', 'ARPEGGIO', [ARP]),
        ('lfo', 'LFO', [LFO]),
        ('perf', 'PERFORMANCE', [PERF]),
    ]),
    ('setup', 'SETUP', [
        ('master', 'MASTER · MODEL', [MASTER, MODEL]),
        ('voices', 'VOICES · KEYS', [VOICES, KEYBOARD]),
        ('info', 'INFO', [INFO]),
    ]),
]

PANEL_W = 460
PANEL_H = 284


def esc(s):
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
             .replace('"', '&quot;'))


def control_html(c, ports):
    t = c['type']
    if t == 'fader':
        cls = {'uni': 'kr-fader', 'bi': 'kr-fader kr-fader-bi', 'hpf': 'kr-fader kr-fader-hpf'}[c['style']]
        extra = ''
        if c['style'] == 'hpf':
            extra = '<div class="kr-hpf-marks"><span>3</span><span>2</span><span>1</span><span>0</span></div>'
        return ('<div class="kr-ctl"><div class="kr-lbl">%s</div><div class="kr-fader-wrap">'
                '<div class="%s" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="film"></div>%s'
                '</div></div>' % (esc(c['label']), cls, c['symbol'], extra))
    if t == 'switch':
        n = len(ports[c['symbol']]['points']) or 2
        labels = ''.join('<span>%s</span>' % esc(x) for x in c['labels'])
        return ('<div class="kr-ctl kr-sw-ctl"><div class="kr-lbl">%s</div><div class="kr-sw-wrap">'
                '<div class="kr-sw kr-sw%d" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="film"></div>'
                '<div class="kr-sw-labels kr-sw-labels%d">%s</div></div></div>'
                % (esc(c['label']), n, c['symbol'], n, labels))
    if t == 'hswitch':
        return ('<div class="kr-ctl kr-hsw-ctl"><div class="kr-lbl">%s</div><div class="kr-hsw-wrap">'
                '<span>%s</span><div class="kr-hsw" mod-role="input-control-port" mod-port-symbol="%s" mod-widget="film"></div>'
                '<span>%s</span></div></div>' % (esc(c['label']), esc(c['left']), c['symbol'], esc(c['right'])))
    if t == 'pad':
        return ('<div class="kr-ctl kr-pad-ctl"><div class="kr-pad kr-pad-%s" mod-role="input-control-port" '
                'mod-port-symbol="%s" mod-widget="film"></div><div class="kr-lbl kr-lbl-below">%s</div></div>'
                % (c['color'], c['symbol'], esc(c['label'])))
    if t == 'stack':
        return '<div class="kr-stack">%s</div>' % ''.join(control_html(i, ports) for i in c['items'])
    if t == 'select':
        p = ports[c['symbol']]
        if p['points']:
            opts = [(pt['value'], pt['label']) for pt in p['points']]
        else:
            opts = [(v, ('%+d' % v) if v else '0') for v in range(int(p['min']), int(p['max']) + 1)]
        options = ''.join('<div mod-role="enumeration-option" mod-port-value="%s">%s</div>'
                          % (('%g' % v), esc(lbl.upper())) for v, lbl in opts)
        return ('<div class="kr-ctl kr-sel-ctl"><div class="kr-lbl">%s</div>'
                '<div class="mod-enumerated kr-select" style="width:%dpx" mod-role="input-control-port" '
                'mod-port-symbol="%s" mod-widget="custom-select">'
                '<div class="kr-select-value" mod-role="input-control-value" mod-port-symbol="%s"></div>'
                '<div class="mod-enumerated-list kr-select-list">%s</div></div></div>'
                % (esc(c['label']), c['width'], c['symbol'], c['symbol'], options))
    if t == 'chorus':
        return ''.join(
            '<div class="kr-ctl kr-pad-ctl"><div class="kr-pad kr-pad-%s kr-chorus" data-chorus="%s"></div>'
            '<div class="kr-lbl kr-lbl-below">%s</div></div>' % (col, key, lbl)
            for key, lbl, col in (('off', 'OFF', 'cream'), ('1', 'I', 'yellow'), ('2', 'II', 'orange')))
    if t == 'info':
        return ('<div class="kr-info"><b>Ultramaster KR-106</b> &mdash; Juno-6/60/106 emulation.<br>'
                'Presets: plugin menu (J60 / J106 banks).<br>'
                'MIDI: notes &middot; pitch bend &middot; mod wheel = LFO trig &middot; sustain = hold.<br>'
                'Knobs: plugin settings (&#9881;) &rarr; MIDI learn or actuator.</div>')
    raise ValueError(t)


def build_html(ports):
    tabs = ''.join('<div class="kr-tab%s" data-tab="%s">%s</div>' % (' kr-active' if i == 0 else '', key, esc(title))
                   for i, (key, title, _) in enumerate(TABS))
    subnavs, pages = [], []
    for i, (key, title, subs) in enumerate(TABS):
        subnavs.append('<div class="kr-subnav%s" data-tab="%s">%s</div>' % (
            ' kr-active' if i == 0 else '', key,
            ''.join('<div class="kr-subtab%s" data-tab="%s" data-sub="%s"><span class="kr-subled"></span>%s</div>'
                    % (' kr-active' if j == 0 else '', key, sub, esc(stitle))
                    for j, (sub, stitle, _) in enumerate(subs))))
        for j, (sub, stitle, sections) in enumerate(subs):
            secs = []
            for name, color, controls in sections:
                body = ''.join(control_html(c, ports) for c in controls)
                secs.append('<div class="kr-section kr-%s"><div class="kr-sec-title">%s</div>'
                            '<div class="kr-sec-body">%s</div><div class="kr-sec-foot"></div></div>'
                            % (color, esc(name), body))
            pages.append('<div class="kr-page%s" data-page="%s/%s">%s</div>'
                         % (' kr-active' if i == 0 and j == 0 else '', key, sub, ''.join(secs)))

    jacks_in = '''
    <div class="mod-pedal-input">
        {{#effect.ports.midi.input}}
        <div class="mod-input mod-input-disconnected" title="{{name}}" mod-role="input-midi-port" mod-port-symbol="{{symbol}}">
            <div class="mod-pedal-input-image"></div>
        </div>
        {{/effect.ports.midi.input}}
    </div>'''
    jacks_out = '''
    <div class="mod-pedal-output">
        {{#effect.ports.audio.output}}
        <div class="mod-output mod-output-disconnected" title="{{name}}" mod-role="output-audio-port" mod-port-symbol="{{symbol}}">
            <div class="mod-pedal-output-image"></div>
        </div>
        {{/effect.ports.audio.output}}
    </div>'''

    return '''<div class="mod-pedal kr106{{{cns}}}">
    <div mod-role="drag-handle" class="mod-drag-handle"></div>
    <div class="kr-cheek kr-cheek-l"></div><div class="kr-cheek kr-cheek-r"></div>
    <div class="kr-header">
        <div class="kr-power" title="Power (bypass)">
            <div class="kr-power-led" mod-role="bypass-light"></div>
            <div class="kr-power-sw" mod-role="bypass"></div>
            <div class="kr-power-lbl">POWER</div>
        </div>
        <div class="kr-logo"><span class="kr-logo-brand">ULTRAMASTER</span><span class="kr-logo-model">KR-106</span></div>
        <div class="kr-lcd"><div class="kr-lcd-name">KR-106</div><div class="kr-lcd-value">READY</div></div>
    </div>
    <div class="kr-nav"><div class="kr-tabs">%s</div>%s</div>
    <div class="kr-pages">%s</div>%s%s
</div>
''' % (tabs, ''.join(subnavs), ''.join(pages), jacks_in, jacks_out)


CSS = r'''/* Ultramaster KR-106 modgui -- generated by tools/gen_modgui.py */
@font-face {
    font-family: 'KR106 Barlow';
    font-style: italic;
    font-weight: 600;
    src: url(/resources/fonts/barlow-condensed-600-italic.woff2{{{ns}}}) format('woff2');
}
@font-face {
    font-family: 'KR106 Barlow';
    font-style: italic;
    font-weight: 700;
    src: url(/resources/fonts/barlow-condensed-700-italic.woff2{{{ns}}}) format('woff2');
}
@font-face {
    font-family: 'KR106 Segment';
    src: url(/resources/fonts/Segment14.otf{{{ns}}}) format('opentype');
}

.kr106{{{cns}}} {
    position: relative;
    width: @W@px;
    height: @H@px;
    background:
        linear-gradient(180deg, rgba(255,255,255,0.05), rgba(255,255,255,0) 40%),
        repeating-linear-gradient(90deg, rgba(255,255,255,0.012) 0 1px, rgba(0,0,0,0.015) 1px 3px),
        #2a2a2c;
    border-radius: 3px;
    font-family: 'KR106 Barlow', 'Barlow Condensed', 'Arial Narrow', 'Liberation Sans Narrow', sans-serif;
    font-style: italic;
    font-weight: 600;
    color: #f2f2ee;
    -webkit-user-select: none;
    user-select: none;
    box-sizing: border-box;
}
.kr106{{{cns}}} * {
    box-sizing: border-box;
    /* mod-ui sets `* { font-family: ... !important }`: override it for our panel */
    font-family: 'KR106 Barlow', 'Barlow Condensed', 'Arial Narrow', 'Liberation Sans Narrow', sans-serif !important;
}
.kr106{{{cns}}} .kr-lcd, .kr106{{{cns}}} .kr-lcd *,
.kr106{{{cns}}} .kr-select, .kr106{{{cns}}} .kr-select * {
    font-family: 'KR106 Segment', 'DejaVu Sans Mono', monospace !important;
}
.kr106{{{cns}}} .mod-drag-handle {
    position: absolute; left: 0; top: 0; right: 0; bottom: 0;
    z-index: 1;
    cursor: move;
}

/* end cheeks (Juno-106: black metal, red accent line) */
.kr106{{{cns}}} .kr-cheek {
    position: absolute; top: 0; bottom: 0; width: 12px; z-index: 2; pointer-events: none;
    background: linear-gradient(90deg, #0c0c0d, #2b2b2e 45%, #0c0c0d);
}
.kr106{{{cns}}} .kr-cheek-l { left: 0; border-radius: 3px 0 0 3px; }
.kr106{{{cns}}} .kr-cheek-r { right: 0; border-radius: 0 3px 3px 0; }

/* ---- header ---- */
.kr106{{{cns}}} .kr-header {
    position: absolute; left: 12px; right: 12px; top: 0; height: 42px;
    background: linear-gradient(180deg, #19191b, #0d0d0e);
    border-bottom: 3px solid #c8201c;
    display: flex; align-items: center;
    padding: 0 12px;
    z-index: 3;
    pointer-events: none;
}
.kr106{{{cns}}} .kr-header > * { pointer-events: auto; }
.kr106{{{cns}}} .kr-power { position: relative; width: 84px; height: 32px; flex: none; }
.kr106{{{cns}}} .kr-power-led {
    position: absolute; left: 2px; top: 11px; width: 10px; height: 10px; border-radius: 50%;
    background: radial-gradient(circle at 35% 30%, #8a3a33, #3a0d0b 70%);
    box-shadow: 0 0 0 1.5px #000;
}
.kr106{{{cns}}} .kr-power-led.on {
    background: radial-gradient(circle at 35% 30%, #ffd6cc, #f01a10 55%, #a00 100%);
    box-shadow: 0 0 0 1.5px #000, 0 0 8px 2px rgba(255,40,30,0.55);
}
.kr106{{{cns}}} .kr-power-sw {
    position: absolute; left: 18px; top: 5px; width: 18px; height: 22px; cursor: pointer;
    border-radius: 2px; background: #000; box-shadow: inset 0 0 0 1px #555;
}
.kr106{{{cns}}} .kr-power-sw::after {
    content: ''; position: absolute; left: 3px; right: 3px; top: 3px; height: 9px; border-radius: 1px;
    background: linear-gradient(180deg, #e9e9e9, #8d8d8d);
}
.kr106{{{cns}}} .kr-power-sw.off::after { top: 10px; background: linear-gradient(180deg, #6d6d6d, #cfcfcf); }
.kr106{{{cns}}} .kr-power-lbl { position: absolute; left: 40px; top: 9px; font-size: 11px; letter-spacing: 0.5px; }

.kr106{{{cns}}} .kr-logo { display: flex; align-items: baseline; margin: 0 0 0 4px; flex: none; }
.kr106{{{cns}}} .kr-logo-brand { font-size: 10px; font-weight: 600; letter-spacing: 1.5px; color: #c9c9c4; margin-right: 6px; }
.kr106{{{cns}}} .kr-logo-model { font-size: 25px; font-weight: 700; letter-spacing: 1px; color: #fff; }

/* ---- navigation: main tabs, then one row of sub-tabs per main tab ---- */
.kr106{{{cns}}} .kr-nav {
    position: absolute; left: 12px; right: 12px; top: 45px; height: 56px;
    padding: 4px 8px 0; z-index: 3;
    background: linear-gradient(180deg, #1d1d1f, #252527);
    border-bottom: 1px solid #111;
}
.kr106{{{cns}}} .kr-tabs { display: flex; height: 25px; }
.kr106{{{cns}}} .kr-tab {
    flex: 1; text-align: center;
    margin: 0 2px; line-height: 23px; font-size: 13px; letter-spacing: 1px; white-space: nowrap;
    color: #bdbdb8; cursor: pointer;
    border: 1px solid #3a3a3d; border-radius: 2px;
    background: linear-gradient(180deg, #2c2c2f, #1b1b1d);
}
.kr106{{{cns}}} .kr-tab:hover { color: #fff; border-color: #6a6a6e; }
.kr106{{{cns}}} .kr-tab.kr-active {
    color: #fff; border-color: #c8201c;
    background: linear-gradient(180deg, #d42a22, #9e1612);
    box-shadow: 0 0 6px rgba(220,40,30,0.35);
}
.kr106{{{cns}}} .kr-subnav { display: none; height: 22px; margin-top: 4px; }
.kr106{{{cns}}} .kr-subnav.kr-active { display: flex; }
.kr106{{{cns}}} .kr-subtab {
    flex: 1; display: flex; align-items: center; justify-content: center;
    margin: 0 2px; height: 22px; font-size: 11.5px; letter-spacing: 0.8px; white-space: nowrap;
    color: #a9a9a4; cursor: pointer;
    border-radius: 2px; background: #161617; border: 1px solid #2f2f32;
}
.kr106{{{cns}}} .kr-subtab:hover { color: #fff; }
.kr106{{{cns}}} .kr-subtab.kr-active { color: #fff; background: #2e2e31; border-color: #55555a; }
.kr106{{{cns}}} .kr-subled {
    width: 7px; height: 7px; border-radius: 50%; margin-right: 6px; flex: none;
    background: radial-gradient(circle at 35% 30%, #7a2a24, #3a0d0b 70%); box-shadow: 0 0 0 1px #000;
}
.kr106{{{cns}}} .kr-subtab.kr-active .kr-subled {
    background: radial-gradient(circle at 35% 30%, #ffd6cc, #f01a10 55%, #a00 100%);
    box-shadow: 0 0 0 1px #000, 0 0 6px 1px rgba(255,40,30,0.6);
}

.kr106{{{cns}}} .kr-lcd {
    width: 150px; height: 30px; padding: 2px 7px; margin-left: auto;
    background: linear-gradient(180deg, #060a06, #0c140c);
    border: 1px solid #000; border-radius: 2px;
    box-shadow: inset 0 0 6px rgba(0,0,0,0.9), 0 0 0 1px #333;
    font-family: 'KR106 Segment', monospace; font-style: normal; font-weight: normal;
    color: #6dff5a; text-shadow: 0 0 4px rgba(90,255,70,0.55);
    overflow: hidden; white-space: nowrap;
}
.kr106{{{cns}}} .kr-lcd-name { font-size: 10px; line-height: 12px; opacity: 0.75; }
.kr106{{{cns}}} .kr-lcd-value { font-size: 13px; line-height: 15px; }

/* ---- pages & sections ---- */
.kr106{{{cns}}} .kr-pages { position: absolute; left: 12px; right: 12px; top: 102px; bottom: 0; pointer-events: none; }
.kr106{{{cns}}} .kr-page {
    position: absolute; left: 0; top: 0; right: 0; bottom: 0;
    display: flex; align-items: stretch; justify-content: center;
    padding: 8px 6px 10px;
    visibility: hidden;
}
.kr106{{{cns}}} .kr-page.kr-active { visibility: visible; }
.kr106{{{cns}}} .kr-section {
    position: relative; display: flex; flex-direction: column;
    margin: 0 3px; min-width: 52px;
    pointer-events: none;
}
.kr106{{{cns}}} .kr-sec-title {
    height: 19px; line-height: 19px; text-align: center;
    font-size: 15px; font-weight: 700; letter-spacing: 1.5px; color: #fff;
    border-radius: 1px;
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.25), inset 0 -1px 0 rgba(0,0,0,0.25);
}
.kr106{{{cns}}} .kr-sec-body {
    flex: 1; display: flex; align-items: flex-start; justify-content: center;
    padding: 6px 6px 0;
    background: linear-gradient(180deg, rgba(0,0,0,0.12), rgba(0,0,0,0) 30%);
}
.kr106{{{cns}}} .kr-sec-foot { height: 4px; border-radius: 1px; }
.kr106{{{cns}}} .kr-red .kr-sec-title,  .kr106{{{cns}}} .kr-red .kr-sec-foot  { background: linear-gradient(180deg, #d8261f, #b0160f); }
.kr106{{{cns}}} .kr-blue .kr-sec-title, .kr106{{{cns}}} .kr-blue .kr-sec-foot { background: linear-gradient(180deg, #2b47c8, #1a2f96); }
.kr106{{{cns}}} .kr-grey .kr-sec-title, .kr106{{{cns}}} .kr-grey .kr-sec-foot { background: linear-gradient(180deg, #67676b, #48484b); }

.kr106{{{cns}}} .kr-ctl {
    position: relative; display: flex; flex-direction: column; align-items: center;
    margin: 0 2px; pointer-events: auto; z-index: 4;
}
.kr106{{{cns}}} .kr-lbl {
    height: 15px; line-height: 15px; font-size: 12px; letter-spacing: 0.6px; white-space: nowrap;
    color: #f2f2ee; text-align: center;
}
.kr106{{{cns}}} .kr-lbl-below { margin-top: 2px; font-size: 11px; }

/* film strips: frame width = element width; images are 2x, hence background-size */
.kr106{{{cns}}} .kr-fader-wrap { position: relative; }
.kr106{{{cns}}} .kr-fader {
    width: @FW@px; height: @FH@px; cursor: ns-resize;
    background-image: url(/resources/fader.png{{{ns}}});
    background-size: @FADER_BG@px @FH@px;
    background-repeat: no-repeat;
}
.kr106{{{cns}}} .kr-fader-bi { background-image: url(/resources/fader-bipolar.png{{{ns}}}); background-size: @FADERBI_BG@px @FH@px; }
.kr106{{{cns}}} .kr-fader-hpf { background-image: url(/resources/fader-hpf.png{{{ns}}}); background-size: @FADERHPF_BG@px @FH@px; }
.kr106{{{cns}}} .kr-hpf-marks {
    position: absolute; left: 31px; top: 6px; height: @HPFMARKS_H@px;
    display: flex; flex-direction: column; justify-content: space-between;
    font-size: 10px; color: #bdbdb8; line-height: 10px;
}

.kr106{{{cns}}} .kr-sw-ctl { margin: 0 3px; }
.kr106{{{cns}}} .kr-sw-wrap { display: flex; align-items: center; margin-top: 18px; }
.kr106{{{cns}}} .kr-sw {
    width: @SWW@px; height: @SWH@px; cursor: pointer;
    background-repeat: no-repeat;
}
.kr106{{{cns}}} .kr-sw2 { background-image: url(/resources/switch2.png{{{ns}}}); background-size: @SW2_BG@px @SWH@px; }
.kr106{{{cns}}} .kr-sw3 { background-image: url(/resources/switch3.png{{{ns}}}); background-size: @SW3_BG@px @SWH@px; }
.kr106{{{cns}}} .kr-sw-labels {
    display: flex; flex-direction: column; justify-content: space-between;
    height: @SWH@px; margin-left: 4px; padding: 3px 0;
    font-size: 10.5px; line-height: 11px; color: #e6e6e0; white-space: nowrap;
}
.kr106{{{cns}}} .kr-sw-labels2 { padding: 6px 0; }

.kr106{{{cns}}} .kr-hsw-wrap { display: flex; align-items: center; margin-top: 38px; font-size: 13px; }
.kr106{{{cns}}} .kr-hsw-wrap span { margin: 0 6px; }
.kr106{{{cns}}} .kr-hsw {
    width: @SWH@px; height: @SWW@px; cursor: pointer;
    background-image: url(/resources/switch2h.png{{{ns}}}); background-size: @SW2H_BG@px @SWW@px;
    background-repeat: no-repeat;
}

.kr106{{{cns}}} .kr-pad-ctl { margin: 15px 3px 0; }
.kr106{{{cns}}} .kr-pad {
    width: @PW@px; height: @PH@px; cursor: pointer;
    background-repeat: no-repeat; background-size: @PAD_BG@px @PH@px;
}
.kr106{{{cns}}} .kr-pad-cream  { background-image: url(/resources/pad-cream.png{{{ns}}}); }
.kr106{{{cns}}} .kr-pad-yellow { background-image: url(/resources/pad-yellow.png{{{ns}}}); }
.kr106{{{cns}}} .kr-pad-orange { background-image: url(/resources/pad-orange.png{{{ns}}}); }
.kr106{{{cns}}} .kr-pad-grey   { background-image: url(/resources/pad-grey.png{{{ns}}}); }
.kr106{{{cns}}} .kr-chorus.kr-on { background-position: -@PW@px 0; }
.kr106{{{cns}}} .kr-stack { display: flex; flex-direction: column; pointer-events: none; }
.kr106{{{cns}}} .kr-stack .kr-pad-ctl { margin-top: 0; margin-bottom: 4px; }
.kr106{{{cns}}} .kr-stack .kr-pad-ctl:first-child { margin-top: 2px; }

/* LCD-style selector (custom-select widget) */
.kr106{{{cns}}} .kr-sel-ctl { margin: 0 4px; }
.kr106{{{cns}}} .kr-select {
    position: relative; height: 24px; margin-top: 30px; cursor: pointer;
    background: linear-gradient(180deg, #060a06, #0c140c);
    border: 1px solid #000; border-radius: 2px;
    box-shadow: inset 0 0 5px rgba(0,0,0,0.9), 0 0 0 1px #3a3a3d;
    font-family: 'KR106 Segment', monospace; font-style: normal; font-weight: normal;
    color: #6dff5a; text-shadow: 0 0 4px rgba(90,255,70,0.5);
}
.kr106{{{cns}}} .kr-select::after {
    content: ''; position: absolute; right: 5px; top: 9px;
    border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #4fbf42;
}
.kr106{{{cns}}} .kr-select-value {
    font-size: 11px; line-height: 22px; padding: 0 16px 0 6px;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis; text-transform: uppercase;
}
.kr106{{{cns}}} .kr-select .mod-enumerated-list {
    display: none; position: absolute; left: -1px; top: 24px; min-width: 100%; max-height: 150px;
    overflow-y: auto; z-index: 50;
    background: #070b07; border: 1px solid #2e5a28; box-shadow: 0 6px 14px rgba(0,0,0,0.7);
}
.kr106{{{cns}}} .kr-select .mod-enumerated-list div {
    font-size: 11px; line-height: 20px; padding: 0 6px; white-space: nowrap; color: #5fd84f;
}
.kr106{{{cns}}} .kr-select .mod-enumerated-list div:hover { background: #163015; color: #b8ffae; }
.kr106{{{cns}}} .kr-select .mod-enumerated-list div.selected { background: #245a1f; color: #e6ffe0; }

.kr106{{{cns}}} .kr-info {
    width: 250px; margin-top: 8px; padding: 8px 10px; pointer-events: auto; z-index: 4;
    font-size: 12.5px; line-height: 17px; font-weight: 600; color: #d6d6d0;
    background: rgba(0,0,0,0.25); border: 1px solid #3a3a3d; border-radius: 2px;
}
.kr106{{{cns}}} .kr-info b { color: #fff; font-weight: 700; }

/* disabled (addressed to an actuator) */
.kr106{{{cns}}} [mod-role=input-control-port].disabled { opacity: 0.55; cursor: not-allowed; }

/* MOD jacks sit outside the panel */
.kr106{{{cns}}} .mod-pedal-input,
.kr106{{{cns}}} .mod-pedal-output { top: 120px; }
'''


JS = r'''// Ultramaster KR-106 modgui -- generated by tools/gen_modgui.py
function (event, funcs) {
    var icon = event.icon;
    var data = event.data;

    var PORTS = @PORTS@;

    function showSub(tab, sub) {
        icon.find('.kr-subtab[data-tab="' + tab + '"]').removeClass('kr-active');
        icon.find('.kr-subtab[data-tab="' + tab + '"][data-sub="' + sub + '"]').addClass('kr-active');
        icon.find('.kr-page').removeClass('kr-active');
        icon.find('.kr-page[data-page="' + tab + '/' + sub + '"]').addClass('kr-active');
        data.sub[tab] = sub;
    }

    function showTab(tab) {
        icon.find('.kr-tab').removeClass('kr-active');
        icon.find('.kr-tab[data-tab="' + tab + '"]').addClass('kr-active');
        icon.find('.kr-subnav').removeClass('kr-active');
        icon.find('.kr-subnav[data-tab="' + tab + '"]').addClass('kr-active');
        data.tab = tab;
        showSub(tab, data.sub[tab] || icon.find('.kr-subtab[data-tab="' + tab + '"]').first().attr('data-sub'));
    }

    function updateChorus(mode) {
        mode = Math.round(mode);
        icon.find('.kr-chorus[data-chorus="off"]').toggleClass('kr-on', mode === 0);
        icon.find('.kr-chorus[data-chorus="1"]').toggleClass('kr-on', (mode & 1) !== 0);
        icon.find('.kr-chorus[data-chorus="2"]').toggleClass('kr-on', (mode & 2) !== 0);
    }

    function formatValue(p, value) {
        if (p.points.length) {
            for (var i = 0; i < p.points.length; i++)
                if (Math.abs(p.points[i].value - value) < 0.01) return p.points[i].label;
        }
        if (p.kind === 'toggle') return value > 0.5 ? 'ON' : 'OFF';
        if (p.kind === 'int') return (value > 0 ? '+' : '') + Math.round(value);
        if (p.min < 0) {
            var v = Math.round(value * 64);
            return (v > 0 ? '+' : '') + v;
        }
        // Juno-106 style 7-bit slider value
        return String(Math.round((value - p.min) / (p.max - p.min) * 127));
    }

    function showLcd(symbol, value) {
        var p = PORTS[symbol];
        if (!p) return;
        icon.find('.kr-lcd-name').text(p.name.toUpperCase());
        icon.find('.kr-lcd-value').text(String(formatValue(p, value)).toUpperCase());
    }

    if (event.type === 'start') {
        // mod-ui replays every port value right after 'start': keep the LCD quiet meanwhile
        data.quietUntil = Date.now() + 1000;
        data.chorus = 1;
        for (var i = 0; i < event.ports.length; i++) {
            if (event.ports[i].symbol === 'chorus') data.chorus = event.ports[i].value;
        }
        updateChorus(data.chorus);
        data.sub = data.sub || {};
        showTab(data.tab || 'sound');

        icon.find('.kr-tab').on('click', function (e) {
            e.stopPropagation();
            showTab($(this).attr('data-tab'));
        });
        icon.find('.kr-subtab').on('click', function (e) {
            e.stopPropagation();
            showSub($(this).attr('data-tab'), $(this).attr('data-sub'));
        });

        icon.find('.kr-chorus').on('mousedown', function (e) {
            e.preventDefault();
            e.stopPropagation();
        }).on('click', function (e) {
            e.stopPropagation();
            var key = $(this).attr('data-chorus');
            var mode = Math.round(data.chorus);
            if (key === 'off') mode = 0;
            else mode = mode ^ parseInt(key, 10);
            data.chorus = mode;
            funcs.set_port_value('chorus', mode);
            updateChorus(mode);
            showLcd('chorus', mode);
        });
        return;
    }

    if (event.type === 'change') {
        if (event.symbol === 'chorus') {
            data.chorus = event.value;
            updateChorus(event.value);
        }
        if (Date.now() > data.quietUntil) showLcd(event.symbol, event.value);
    }
}
'''


MODGUI_TTL = '''@prefix lv2:    <http://lv2plug.in/ns/lv2core#> .
@prefix modgui: <http://moddevices.com/ns/modgui#> .

# Generated by tools/gen_modgui.py
<https://kayrock.org/kr106/mod>
    modgui:gui [
        modgui:resourcesDirectory <modgui> ;
        modgui:iconTemplate <modgui/icon-kr106.html> ;
        modgui:stylesheet <modgui/stylesheet-kr106.css> ;
        modgui:javascript <modgui/script-kr106.js> ;
        modgui:screenshot <modgui/screenshot-kr106.png> ;
        modgui:thumbnail <modgui/thumbnail-kr106.png> ;
        modgui:brand "Kayrock" ;
        modgui:label "KR-106" ;
    ] .
'''


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    ports_list = json.load(open(sys.argv[1]))
    ports = {p['symbol']: p for p in ports_list}
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)

    # every port must appear on the panel exactly once
    used = []

    def collect(c):
        if c['type'] == 'stack':
            for i in c['items']:
                collect(i)
        elif c['type'] == 'chorus':
            used.append('chorus')
        elif 'symbol' in c:
            used.append(c['symbol'])
    for _, _, subs in TABS:
        for _, _, sections in subs:
            for _, _, controls in sections:
                for c in controls:
                    collect(c)
    missing = set(ports) - set(used)
    unknown = set(used) - set(ports)
    dupes = set(s for s in used if used.count(s) > 1)
    if missing or unknown or dupes:
        sys.exit('layout error: missing=%s unknown=%s duplicated=%s' % (sorted(missing), sorted(unknown), sorted(dupes)))

    # --- images ---
    ticks11 = [i / 10 for i in range(11)]
    save(make_fader(128, ticks11, {0, 5, 10}), os.path.join(out, 'fader.png'))
    save(make_fader(129, ticks11, {0, 5, 10}), os.path.join(out, 'fader-bipolar.png'))
    save(make_fader(4, [0, 1 / 3, 2 / 3, 1], {0, 1, 2, 3}), os.path.join(out, 'fader-hpf.png'))
    save(make_switch(2), os.path.join(out, 'switch2.png'))
    save(make_switch(3), os.path.join(out, 'switch3.png'))
    save(make_switch(2, horizontal=True), os.path.join(out, 'switch2h.png'))
    for color in PAD_COLORS:
        save(make_pad(color), os.path.join(out, 'pad-%s.png' % color))

    # --- html / css / js / ttl ---
    with open(os.path.join(out, 'icon-kr106.html'), 'w') as f:
        f.write(build_html(ports))

    subst = {
        '@W@': PANEL_W, '@H@': PANEL_H,
        '@FW@': FADER_W, '@FH@': FADER_H,
        '@FADER_BG@': FADER_W * 128, '@FADERBI_BG@': FADER_W * 129, '@FADERHPF_BG@': FADER_W * 4,
        '@HPFMARKS_H@': FADER_BOT - FADER_TOP + 10,
        '@SWW@': SW_W, '@SWH@': SW_H,
        '@SW2_BG@': SW_W * 2, '@SW3_BG@': SW_W * 3, '@SW2H_BG@': SW_H * 2,
        '@PW@': PAD_W, '@PH@': PAD_H, '@PAD_BG@': PAD_W * 2,
    }
    css = CSS
    for k, v in subst.items():
        css = css.replace(k, str(v))
    with open(os.path.join(out, 'stylesheet-kr106.css'), 'w') as f:
        f.write(css)

    js_ports = {p['symbol']: {'name': p['name'], 'kind': p['kind'], 'min': p['min'], 'max': p['max'],
                              'points': p['points']} for p in ports_list}
    with open(os.path.join(out, 'script-kr106.js'), 'w') as f:
        f.write(JS.replace('@PORTS@', json.dumps(js_ports, separators=(',', ':'))))

    with open(os.path.join(os.path.dirname(os.path.abspath(out)), 'modgui.ttl'), 'w') as f:
        f.write(MODGUI_TTL)
    print('modgui written to', out)


if __name__ == '__main__':
    main()
