FROM alpine:latest

WORKDIR /app
COPY server-c/build/cloud_clipboard /app/

VOLUME /tmp/cloud_clipboard
EXPOSE 8123

CMD ["./cloud_clipboard"]

