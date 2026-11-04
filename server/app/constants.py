import re

NOTE_W = 160
NOTE_H = 120
NOTE_BYTES = 2400  # 160*120 / 8
MAX_RECIPIENTS = 10
SENDS_PER_HOUR = 30
HANDLE_RE = re.compile(r"[a-z0-9_]{3,16}")
MIN_PASSWORD = 8
MAX_PASSWORD = 63  # Palm password field
MAX_USERNAME = 32  # Palm username field
# Palm fields are Latin-1, not UTF-8: restrict to printable ASCII (no spaces in usernames;
# no '|' or controls, which would break line-oriented formats).
USERNAME_RE = re.compile(r"[\x21-\x7b\x7d-\x7e]{1,32}")  # printable ASCII, no space, no '|'
PASSWORD_RE = re.compile(r"[\x20-\x7e]{8,63}")
LOGIN_FAIL_LIMIT = 5
LOGIN_FAIL_WINDOW = 900  # seconds
WEB_SESSION_TTL = 30 * 24 * 3600  # seconds a web session stays valid
