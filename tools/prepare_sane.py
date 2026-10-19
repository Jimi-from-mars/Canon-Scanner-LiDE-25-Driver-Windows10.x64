"""Small, repeatable Windows port patches; original copyrights stay intact."""
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
base=root/'third_party/sane/backend'
p=base/'plustek-usbhw.c'
s=p.read_text()
old='usb_IsEscPressed( void )\n{\n\tsigset_t sigs;'
if old in s:
    s=s.replace(old,'usb_IsEscPressed( void )\n{\n#ifdef _WIN32\n\treturn cancelRead;\n#else\n\tsigset_t sigs;')
    start=s.index('usb_IsEscPressed( void )')
    end=s.index('\n}',start)
    s=s[:end]+'\n#endif'+s[end:]
p.write_text(s,newline='\n')
if 'void l25_backend_cancel(void)' not in s:
    with p.open('a',newline='\n') as f:f.write('\n/* Native Windows cooperative cancellation entry point. */\nvoid l25_backend_cancel(void) { cancelRead = SANE_TRUE; l25_abort_channel(); }\n')
if 'void l25_backend_clear_cancel(void)' not in s:
    with p.open('a',newline='\n') as f:f.write('\nvoid l25_backend_clear_cancel(void) { cancelRead = SANE_FALSE; }\n')
p=base/'plustek.c';s=p.read_text()
# Windows cancellation is cooperative; the pipe writer must stop on a closed reader.
s=s.replace('write( scanner->w_pipe, buf, scanner->params.bytes_per_line );',
    'if (write(scanner->w_pipe, buf, scanner->params.bytes_per_line) != scanner->params.bytes_per_line) { status = _E_ABORT; break; }')
s=re.sub(r'\b(read|write|close)\s*\(',lambda m:'l25_pipe_'+m.group(1)+'(',s)
s=s.replace('nread = l25_pipe_read( s->r_pipe, buf, sizeof(ipc));',
 '''nread = l25_pipe_read( s->r_pipe, buf, sizeof(ipc));
            if (nread == 0) { s->exit_code = sanei_thread_get_status(s->reader_pid); sanei_thread_invalidate(s->reader_pid); s->scanning = SANE_FALSE; drvclose(s->hw); close_pipe(s); return s->exit_code == SANE_STATUS_GOOD ? SANE_STATUS_IO_ERROR : s->exit_code; }''') if 'if (nread == 0) { s->exit_code' not in s else s
p.write_text(s,newline='\n')
