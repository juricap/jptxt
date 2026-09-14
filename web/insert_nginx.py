from pathlib import Path

p = Path("/etc/nginx/sites-available/whatamieatingtoday.com")
t = p.read_text()
marker = "    include /etc/nginx/snippets/jptxt.conf;"
if marker in t:
    print("already included")
    raise SystemExit(0)
needle = """    location /apps/ {
        alias /var/www/apps/;
        index index.html;
        try_files $uri $uri/ $uri/index.html =404;
    }
"""
if needle not in t:
    raise SystemExit("apps location block not found in vhost")
t = t.replace(needle, needle + "\n    include /etc/nginx/snippets/jptxt.conf;\n", 1)
p.write_text(t)
print("inserted include")
