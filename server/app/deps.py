from fastapi import Request

from . import db


def get_conn(request: Request):
    conn = db.connect(request.app.state.settings.db_path)
    try:
        yield conn
    finally:
        conn.close()
