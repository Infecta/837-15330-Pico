import http from 'node:http';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const root = path.dirname(fileURLToPath(import.meta.url));
const types = {'.html':'text/html; charset=utf-8', '.css':'text/css; charset=utf-8', '.js':'text/javascript; charset=utf-8'};
http.createServer(async (req, res) => {
  try {
    const pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
    const file = path.resolve(root, '.' + (pathname === '/' ? '/index.html' : pathname));
    if (!file.startsWith(root + path.sep) && file !== path.join(root, 'index.html')) {
      res.writeHead(403); res.end(); return;
    }
    const data = await readFile(file);
    res.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff'});
    res.end(data);
  } catch { res.writeHead(404); res.end('Not found'); }
}).listen(8371, '127.0.0.1', () => console.log('slidrr setup: http://127.0.0.1:8371'));
