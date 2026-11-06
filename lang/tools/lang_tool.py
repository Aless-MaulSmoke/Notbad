#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ferramenta de idiomas do Notbad.

O ingles do res/notbad.rc e a fonte da verdade. O lang/en.json e derivado dele.

  python tools/lang_tool.py --write
      Regera lang/en.json a partir do res/notbad.rc.

  python tools/lang_tool.py --check
      Confere se lang/en.json esta em dia com o res/notbad.rc.

  python tools/lang_tool.py --check lang/pt-BR.json [outros.json ...]
      Valida arquivos de traducao contra o ingles: chaves desconhecidas,
      chaves faltando, valores vazios e formatadores printf (%s, %d, %u...)
      incompativeis nas strings da stringtable.

Chaves do JSON:
  IDM_*            itens de menu (texto completo, com o "\\tCtrl+X" do atalho)
  IDM_POPUP_*      titulos dos menus (File, Edit, Format, Encoding, View, Help)
  IDD_*            titulo de dialogo (so os que nao usam IDS_*)
  IDC_*            texto de controles de dialogo
  IDS_*            strings da stringtable (mensagens, status, titulos)
  _*               metadados, ignorados pelo aplicativo
"""

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RC_PATH = os.path.join(ROOT, 'res', 'notbad.rc')
EN_PATH = os.path.join(ROOT, 'lang', 'en.json')

# Controles que nao entram no JSON: nomes proprios, URL, imagem, texto dinamico.
SKIP_CONTROL_IDS = {
    'IDC_ABOUT_NAME',      # "Notbad"
    'IDC_ABOUT_VERSION',   # montado por codigo (IDS_ABOUT_VERSION)
    'IDC_ABOUT_LINK',      # URL
    'IDC_ABOUT_IMAGE',
    'IDC_FIND_STATUS',     # preenchido por codigo
}

# Dialogos cujo titulo vem da stringtable (IDS_ABOUT_TITLE, IDS_FIND_TITLE/IDS_REPLACE_TITLE).
SKIP_CAPTIONS = {'IDD_ABOUT', 'IDD_FIND'}

# IDOK/IDCANCEL sao compartilhados entre dialogos; cada um ganha chave propria.
SYSTEM_BUTTON_KEYS = {
    ('IDD_FIND', 'IDCANCEL'): 'IDC_FIND_CLOSE',
    ('IDD_INDENT', 'IDOK'): 'IDC_INDENT_OK',
    ('IDD_INDENT', 'IDCANCEL'): 'IDC_INDENT_CANCEL',
}

STRING_RE = r'"((?:[^"]|"")*)"'
CONTROL_RE = re.compile(
    r'^\s*(?:LTEXT|CTEXT|RTEXT|DEFPUSHBUTTON|PUSHBUTTON|GROUPBOX|AUTOCHECKBOX|'
    r'AUTORADIOBUTTON|CHECKBOX|RADIOBUTTON|CONTROL)\s+' + STRING_RE + r'\s*,\s*(\w+)')


class ToolError(Exception):
    pass


def rc_unescape(raw):
    """Converte o texto de um literal do RC no texto real."""
    out = []
    i = 0
    while i < len(raw):
        ch = raw[i]
        if ch == '"':
            if i + 1 < len(raw) and raw[i + 1] == '"':
                out.append('"')
                i += 2
                continue
            raise ToolError('aspas sem par em: %r' % raw)
        if ch == '\\':
            if i + 1 >= len(raw):
                raise ToolError('barra no fim de: %r' % raw)
            nxt = raw[i + 1]
            if nxt == 't':
                out.append('\t')
            elif nxt == 'n':
                out.append('\n')
            elif nxt == '\\':
                out.append('\\')
            else:
                raise ToolError('escape nao suportado \\%s em: %r' % (nxt, raw))
            i += 2
            continue
        out.append(ch)
        i += 1
    return ''.join(out)


def read_rc():
    with open(RC_PATH, 'rb') as f:
        data = f.read()
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        text = data.decode('cp1252')
    return text.replace('\r\n', '\n').split('\n')


def block_end(lines, start):
    """Indice da linha END que fecha o BEGIN do bloco iniciado em start."""
    depth = 0
    started = False
    for i in range(start, len(lines)):
        word = lines[i].strip().split(' ')[0]
        if word == 'BEGIN':
            depth += 1
            started = True
        elif word == 'END':
            depth -= 1
            if started and depth == 0:
                return i
    raise ToolError('bloco sem END a partir da linha %d' % (start + 1))


def parse_menu(lines, out):
    start = next((i for i, l in enumerate(lines) if re.match(r'^IDR_MAINMENU\s+MENU', l)), None)
    if start is None:
        raise ToolError('IDR_MAINMENU nao encontrado')
    end = block_end(lines, start)
    for line in lines[start:end + 1]:
        m = re.match(r'^\s*POPUP\s+' + STRING_RE, line)
        if m:
            label = rc_unescape(m.group(1)).replace('&', '')
            key = 'IDM_POPUP_' + re.sub(r'[^A-Za-z0-9]+', '_', label).upper()
            out[key] = rc_unescape(m.group(1))
            continue
        m = re.match(r'^\s*MENUITEM\s+' + STRING_RE + r'\s*,\s*(\w+)', line)
        if m:
            out[m.group(2)] = rc_unescape(m.group(1))


def parse_dialogs(lines, out):
    for i, line in enumerate(lines):
        m = re.match(r'^(IDD_\w+)\s+DIALOGEX', line)
        if not m:
            continue
        dlg = m.group(1)
        end = block_end(lines, i)
        for body in lines[i:end + 1]:
            mc = re.match(r'^CAPTION\s+' + STRING_RE, body)
            if mc and dlg not in SKIP_CAPTIONS:
                out[dlg] = rc_unescape(mc.group(1))
                continue
            mk = CONTROL_RE.match(body)
            if not mk:
                continue
            text = rc_unescape(mk.group(1))
            ctrl = mk.group(2)
            if ctrl in SKIP_CONTROL_IDS or text == '':
                continue
            if ctrl in ('IDOK', 'IDCANCEL'):
                key = SYSTEM_BUTTON_KEYS.get((dlg, ctrl))
                if key is None:
                    raise ToolError('%s: %s precisa de chave propria em SYSTEM_BUTTON_KEYS' % (dlg, ctrl))
            else:
                key = ctrl
            out[key] = text


def parse_stringtable(lines, out):
    start = next((i for i, l in enumerate(lines) if l.strip() == 'STRINGTABLE'), None)
    if start is None:
        raise ToolError('STRINGTABLE nao encontrada')
    end = block_end(lines, start)
    for line in lines[start:end + 1]:
        m = re.match(r'^\s+(IDS_\w+)\s+' + STRING_RE + r'\s*$', line)
        if m:
            out[m.group(1)] = rc_unescape(m.group(2))


def build_english():
    lines = read_rc()
    out = {}
    parse_menu(lines, out)
    parse_dialogs(lines, out)
    parse_stringtable(lines, out)
    return out


def english_document():
    doc = {
        '_language': 'English',
        '_locale': 'en',
        '_info': ('Notbad base language file. Keys starting with "_" are ignored. '
                  'To translate: copy this file as <locale>.json (for example pt-BR.json) '
                  'and change only the values. Keep the printf specifiers (%s, %d, %u, %%) '
                  'of each value exactly as in English, and keep the \\t shortcut part of menu items.'),
    }
    doc.update(build_english())
    return doc


def dumps(doc):
    return json.dumps(doc, ensure_ascii=False, indent=2) + '\n'


# --- formatadores printf: mesma regra do langcore.h ---------------------------

def format_specs(text):
    """Sequencia de conversoes ("s;d;u;"), ou None se houver construcao nao suportada."""
    specs = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] != '%':
            i += 1
            continue
        i += 1
        if i >= n:
            return None
        if text[i] == '%':
            i += 1
            continue
        while i < n and text[i] in '-+ #0':
            i += 1
        while i < n and text[i].isdigit() and text[i] in '0123456789':
            i += 1
        if i < n and text[i] == '*':
            return None
        if i < n and text[i] == '.':
            i += 1
            while i < n and text[i] in '0123456789':
                i += 1
            if i < n and text[i] == '*':
                return None
        length = ''
        while i < n and text[i] in 'hlLwzjtI':
            length += text[i]
            was_i = text[i] == 'I'
            i += 1
            if was_i:
                while i < n and text[i] in '0123456789':
                    length += text[i]
                    i += 1
        if i >= n or text[i] not in 'cCdiouxXsSpeEfgG':
            return None
        specs.append(length + text[i])
        i += 1
    return ';'.join(specs) + ';' if specs else ''


def check_english_drift():
    expected = dumps(english_document())
    if not os.path.exists(EN_PATH):
        print('FALTA %s (rode --write)' % EN_PATH)
        return 1
    with open(EN_PATH, 'r', encoding='utf-8') as f:
        current = f.read()
    if current == expected:
        print('OK: lang/en.json esta em dia com res/notbad.rc (%d chaves).' % len(build_english()))
        return 0
    cur = json.loads(current)
    exp = json.loads(expected)
    for k in exp:
        if k not in cur:
            print('FALTA no en.json: %s' % k)
        elif cur[k] != exp[k]:
            print('DIFERE: %s\n   json: %r\n   rc  : %r' % (k, cur[k], exp[k]))
    for k in cur:
        if k not in exp:
            print('SOBRA no en.json: %s' % k)
    print('lang/en.json fora de sincronia com res/notbad.rc (rode --write).')
    return 1


def check_translation(path):
    errors = 0
    english = build_english()
    try:
        with open(path, 'rb') as f:
            raw = f.read()
        if raw.startswith(b'\xef\xbb\xbf'):
            raw = raw[3:]
        data = json.loads(raw.decode('utf-8'))
    except (OSError, ValueError) as e:
        print('%s: JSON invalido: %s' % (path, e))
        return 1
    if not isinstance(data, dict) or not all(isinstance(v, str) for v in data.values()):
        print('%s: precisa ser um objeto plano com valores de texto' % path)
        return 1

    unknown = [k for k in data if not k.startswith('_') and k not in english]
    missing = [k for k in english if k not in data]
    empty = [k for k in data if not k.startswith('_') and data[k] == '']

    for k in unknown:
        print('%s: chave desconhecida: %s' % (path, k))
    for k in empty:
        print('%s: valor vazio (sera ignorado): %s' % (path, k))
    for k, base in english.items():
        if k not in data or '%' not in base:
            continue
        if not k.startswith('IDS_'):
            continue
        if format_specs(base) != format_specs(data[k]):
            print('%s: formatadores diferentes em %s\n   en: %r\n   tr: %r' % (path, k, base, data[k]))
            errors += 1
    done = len(english) - len(missing)
    print('%s: %d/%d chaves traduzidas (%d%%); faltando cairao no ingles.' % (
        path, done, len(english), 100 * done // len(english)))
    if missing and '-v' in sys.argv:
        for k in missing:
            print('   falta: %s' % k)
    return 1 if errors or unknown else 0


def main():
    ap = argparse.ArgumentParser(description='Ferramenta de idiomas do Notbad')
    ap.add_argument('--write', action='store_true', help='regera lang/en.json do res/notbad.rc')
    ap.add_argument('--check', nargs='*', metavar='ARQUIVO', help='valida en.json (sem argumentos) ou traducoes')
    ap.add_argument('-v', action='store_true', help='lista chaves faltando')
    args = ap.parse_args()

    try:
        if args.write:
            os.makedirs(os.path.dirname(EN_PATH), exist_ok=True)
            with open(EN_PATH, 'w', encoding='utf-8', newline='\n') as f:
                f.write(dumps(english_document()))
            print('Gerado %s (%d chaves).' % (EN_PATH, len(build_english())))
            return 0
        if args.check is not None:
            if not args.check:
                return check_english_drift()
            return max(check_translation(p) for p in args.check)
    except ToolError as e:
        print('ERRO: %s' % e)
        return 2
    ap.print_help()
    return 0


if __name__ == '__main__':
    sys.exit(main())
