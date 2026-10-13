# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.5.1.bin](kac-0.5.1.bin) | 0.5.1 | 401280 bytes | `dc019104d4d06dc8e98d8536983a4c4f` | `2795d54889ca8803256eeef50b977052e1762509cc1389eee26c1dfb93b3384e` |

Check the download before uploading it:

```sh
md5sum kac-0.5.1.bin        # Linux
md5 kac-0.5.1.bin           # macOS
certutil -hashfile kac-0.5.1.bin MD5   # Windows
```
