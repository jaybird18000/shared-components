#ifndef PWA_FILES_H
#define PWA_FILES_H

static const char kManifestJson[] = R"JSON({
  "name": "Boat Generator Control",
  "short_name": "GenControl",
  "start_url": "/",
  "display": "standalone",
  "theme_color": "#0a3d62",
  "background_color": "#0c1b2a",
  "icons": [
    {
      "src": "/icon.svg",
      "sizes": "128x128",
      "type": "image/svg+xml"
    }
  ]
})JSON";

static const char kServiceWorkerJs[] = R"JS(const CACHE_NAME = 'boat-gen-cache-v1';
const CACHE_FILES = ['/', '/manifest.json', '/service-worker.js', '/icon.svg', '/favicon.ico'];
self.addEventListener('install', event => {
  event.waitUntil(caches.open(CACHE_NAME).then(cache => cache.addAll(CACHE_FILES)));
});
self.addEventListener('fetch', event => {
  const url = event.request.url;

  // Do NOT intercept WebSocket upgrade requests
  if (url.includes('/ws')) {
    return;
  }

  event.respondWith(
    caches.match(event.request).then(resp => resp || fetch(event.request))
  );
});
)JS";

#endif // PWA_FILES_H
