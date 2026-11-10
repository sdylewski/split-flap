# Gzip the web UI (web/) into the folder that `pio run -t uploadfs` packs (.pio/data).
# The web server sends index.html.gz for /index.html automatically - about 4x less to transfer.
Import("env")
import gzip
import hashlib
import os
import shutil

project = env.subst("$PROJECT_DIR")
source = os.path.join(project, "web")
target = env.subst("$PROJECT_DATA_DIR")

def build_web(*args, **kwargs):
    if os.path.isdir(target):
        shutil.rmtree(target)
    os.makedirs(target)
    total_in = total_out = 0
    for name in sorted(os.listdir(source)):
        path = os.path.join(source, name)
        if not os.path.isfile(path) or name.startswith("."):
            continue
        data = open(path, "rb").read()
        if name == "index.html":
            # app.js?v=<hash>: a new version gets a new URL, so no browser keeps an old copy
            for asset in ("app.js", "style.css"):
                digest = hashlib.sha1(open(os.path.join(source, asset), "rb").read()).hexdigest()[:8]
                data = data.replace(('"%s"' % asset).encode(), ('"%s?v=%s"' % (asset, digest)).encode())
        total_in += len(data)
        if name.endswith((".html", ".js", ".css", ".json", ".svg")):
            out = gzip.compress(data, 9, mtime=0)
            open(os.path.join(target, name + ".gz"), "wb").write(out)
            total_out += len(out)
        else:
            shutil.copy(path, target)
            total_out += len(data)
    print("web: %d KB -> %d KB in %s" % (total_in // 1024, total_out // 1024, target))

build_web()
