FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app
COPY demo_app/requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt \
    && useradd --create-home --uid 10001 appuser
COPY dashboard /app/dashboard
USER 10001

EXPOSE 8080
WORKDIR /app/dashboard
CMD ["python3", "server.py", "--host", "0.0.0.0", "--port", "8080"]
