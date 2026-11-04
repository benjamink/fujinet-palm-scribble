# Stage 1: build the React UI.
# Base images pinned by exact tag + digest (both built 2026-09-19, >2 weeks old).
FROM node:22.23.2-slim@sha256:48e4b67d85f87bd551df43704e24d252f56cc5f8e9718841aace50f19948f0f9 AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

# Stage 2: the Python server.
FROM python:3.12.14-slim@sha256:f77ac9e44ae96ef2c90b8053ea08c31f8be030f824196b0ae4db6d462c84e51f
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 PIP_DISABLE_PIP_VERSION_CHECK=1
WORKDIR /app
COPY pyproject.toml ./
COPY app ./app
RUN pip install --no-cache-dir .
COPY --from=web /web/dist /app/web

RUN groupadd --gid 10001 scribble \
 && useradd --uid 10001 --gid 10001 --no-create-home --shell /usr/sbin/nologin scribble \
 && mkdir /data && chown 10001:10001 /data
USER 10001:10001
ENV SCRIBBLE_DB=/data/scribble.db SCRIBBLE_STATIC=/app/web
VOLUME /data
EXPOSE 8000

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD ["python", "-c", "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8000/', timeout=4).status == 200 else 1)"]

# --forwarded-allow-ips="*" is safe only because the host publishes the port on
# 127.0.0.1 (see deploy/scribble.service); only nginx can reach it.
CMD ["uvicorn", "--factory", "app.main:create_app", "--host", "0.0.0.0", "--port", "8000", "--proxy-headers", "--forwarded-allow-ips=*"]
