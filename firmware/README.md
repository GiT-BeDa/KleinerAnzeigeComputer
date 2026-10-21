# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.8.0.bin](kac-0.8.0.bin) | 0.8.0 | 410144 bytes | `304a9f007a24b7e84562a47ac1c6406a` | `f6a64101a127ce6d57d76336cbdcd2722e206270a4ad1b4543a845c60b16e880` |

Check the download before uploading it:

```sh
md5sum kac-0.8.0.bin        # Linux
md5 kac-0.8.0.bin           # macOS
certutil -hashfile kac-0.8.0.bin MD5   # Windows
```
