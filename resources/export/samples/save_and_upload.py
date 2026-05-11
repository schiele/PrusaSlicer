"""
Save and Upload - preFlight Export to Script sample

Saves G-code to a local folder AND uploads via FTPS. Demonstrates
that export scripts can send G-code to multiple destinations.

Modify gcode.data before the outputs to apply last-mile edits
to both destinations at once.
"""
import os
import ftplib
import ssl
import socket
import io

# ---- Local save configuration ----
SAVE_FOLDER = ""        # e.g. "D:/gcode"

# ---- FTPS upload configuration ----
FTP_HOST    = ""        # e.g. "192.168.1.50"
FTP_PORT    = 990
FTP_USER    = ""
FTP_PASS    = ""
FTP_DIR     = "/"

VERIFY_CERT = False
# -----------------------------------


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
    # -- Optional: modify G-code before output --
    # gcode.data.insert(0, "; Processed by preFlight export\n")

    # -- Save locally --
    if SAVE_FOLDER:
        os.makedirs(SAVE_FOLDER, exist_ok=True)
        path = os.path.join(SAVE_FOLDER, gcode.filename)
        with open(path, 'w', encoding='utf-8') as f:
            f.writelines(gcode.data)

    # -- Upload via FTPS --
    if FTP_HOST:
        payload = ''.join(gcode.data).encode('utf-8')
        stream = io.BytesIO(payload)

        ctx = ssl.create_default_context()
        if not VERIFY_CERT:
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE

        ftp = ImplicitFTPS(context=ctx) if FTP_PORT == 990 else ftplib.FTP_TLS(context=ctx)
        ftp.connect(FTP_HOST, FTP_PORT, timeout=15)
        ftp.login(FTP_USER, FTP_PASS)
        ftp.prot_p()
        if FTP_DIR and FTP_DIR != "/":
            ftp.cwd(FTP_DIR)
        ftp.storbinary(f"STOR {gcode.filename}", stream)
        ftp.quit()

    if not SAVE_FOLDER and not FTP_HOST:
        raise RuntimeError("No output configured - edit this script to set SAVE_FOLDER and/or FTP_HOST")
