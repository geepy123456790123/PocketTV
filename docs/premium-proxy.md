# PocketTV Premium Proxy

PocketTV Premium should not hardcode or publish the raw Dispatcharr M3U/EPG URLs. This Worker keeps those upstream URLs in Cloudflare secrets and exposes subscription-gated proxy URLs for the app.

## Endpoints

After deployment, configure PocketTV with:

```text
M3U: https://YOUR_WORKER_DOMAIN/premium/m3u?token=USER_OR_SHARED_TOKEN
EPG: https://YOUR_WORKER_DOMAIN/premium/epg?token=USER_OR_SHARED_TOKEN
```

The M3U response rewrites stream URLs through:

```text
https://YOUR_WORKER_DOMAIN/premium/stream?u=...
```

Logo and XMLTV icon URLs are rewritten through:

```text
https://YOUR_WORKER_DOMAIN/premium/asset?u=...
```

That keeps the raw Dispatcharr host and paths out of the app-visible playlist and EPG.

## Deploy

```bash
cd workers/premium-proxy
npm install
npx wrangler secret put UPSTREAM_M3U_URL
npx wrangler secret put UPSTREAM_EPG_URL
npx wrangler secret put PREMIUM_SHARED_TOKEN
npm run deploy
```

Use the Dispatcharr M3U URL as `UPSTREAM_M3U_URL` and the Dispatcharr XMLTV URL as `UPSTREAM_EPG_URL`. Do not commit those values.

`PREMIUM_SHARED_TOKEN` is good enough for private testing. For real subscriptions, replace it with per-user tokens issued by the subscription website.

## Monetization Path

Use Stripe or Lemon Squeezy on an external website:

1. User subscribes on the website.
2. Backend creates a PocketTV token for that user.
3. PocketTV displays a setup code or accepts the token/URL.
4. Worker validates the token before returning M3U/EPG/stream/asset responses.
5. Canceling a subscription revokes that user's token.

## Rights Note

Only sell access to channels and guide data you have rights to redistribute. The proxy protects URLs and access control, but it does not solve content licensing.
