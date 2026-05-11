"""
FTPS Upload - preFlight Export to Script sample

Uploads G-code to any printer or server that accepts files over
FTPS (implicit TLS, port 990). Streams directly from memory
with no temporary files.

Standard library only - no pip packages required.
"""
import ftplib
import ssl
import socket
import io

# ---- Configuration ----
HOST        = ""            # Printer IP, e.g. "192.168.1.50"
PORT        = 990           # 990 = implicit TLS (common for printers), 21 = plain/explicit
USERNAME    = ""            # FTP username
PASSWORD    = ""            # FTP password or access code
REMOTE_DIR  = "/"           # Remote directory to upload into
VERIFY_CERT = False         # Set True if your server has a trusted certificate
# -----------------------


class ImplicitFTPS(ftplib.FTP_TLS):
    """FTPS with implicit TLS - establishes encryption before the server greeting."""

    def connect(self, host='', port=0, timeout=-999, source_address=None):
        if host:
            self.host = host
        if port:
            self.port = port
        if timeout != -999:
            self.timeout = timeout

        self.sock = socket.create_connection((self.host, self.port), self.timeout)
        self.af = self.sock.family
        self.sock = self.context.wrap_socket(self.sock, server_hostname=self.host)
        self.file = self.sock.makefile('r', encoding=self.encoding)
        self.welcome = self.getresp()
        return self.welcome


def export(gcode):
    if not HOST:
        raise RuntimeError("HOST not configured - edit this script to set your printer's IP address")

    payload = ''.join(gcode.data).encode('utf-8')
    stream = io.BytesIO(payload)

    ctx = ssl.create_default_context()
    if not VERIFY_CERT:
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE

    # Use implicit TLS for port 990, explicit for everything else
    if PORT == 990:
        ftp = ImplicitFTPS(context=ctx)
    else:
        ftp = ftplib.FTP_TLS(context=ctx)

    ftp.connect(HOST, PORT, timeout=15)
    ftp.login(USERNAME, PASSWORD)
    ftp.prot_p()

    if REMOTE_DIR and REMOTE_DIR != "/":
        ftp.cwd(REMOTE_DIR)

    ftp.storbinary(f"STOR {gcode.filename}", stream)
    ftp.quit()
