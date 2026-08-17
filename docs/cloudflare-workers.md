# PocketTV Cloudflare Workers Setup

PocketTV uses three Cloudflare Workers domains:

| Hostname | Worker | Purpose |
| --- | --- | --- |
| `watch.pocket-tv.net` | `workers/landing` | Public signup landing page |
| `api.pocket-tv.net` | `workers/signup-api` | Stripe Checkout, activation-code redemption, subscription verification |
| `stream.pocket-tv.net` | `workers/premium-proxy` | Private M3U, EPG, logo, and stream proxy |

## Prerequisites

1. `pocket-tv.net` must be active in Cloudflare DNS.
2. Wrangler must be logged in:

   ```sh
   npx wrangler login
   npx wrangler whoami
   ```

3. You need a Stripe monthly subscription price ID for the `$9.99/month` plan.

## Create KV

Create one KV namespace for activation-code records:

```sh
cd workers/signup-api
npx wrangler kv namespace create ACTIVATION_CODES
```

Copy the returned `id` into `workers/signup-api/wrangler.jsonc`:

```jsonc
{
  "binding": "ACTIVATION_CODES",
  "id": "YOUR_KV_NAMESPACE_ID"
}
```

## Configure Secrets

### Signup API

```sh
cd workers/signup-api
npx wrangler secret put STRIPE_SECRET_KEY
npx wrangler secret put STRIPE_MONTHLY_PRICE_ID
npx wrangler secret put STRIPE_WEBHOOK_SECRET
```

`SUCCESS_URL`, `CANCEL_URL`, and `PREMIUM_PROXY_BASE_URL` are public values in `wrangler.jsonc`.

Create a Stripe webhook endpoint for:

```text
https://api.pocket-tv.net/api/stripe-webhook
```

Subscribe it to:

```text
checkout.session.completed
customer.subscription.updated
customer.subscription.deleted
```

Copy the webhook signing secret, which starts with `whsec_`, into `STRIPE_WEBHOOK_SECRET`.

### Premium Proxy

```sh
cd workers/premium-proxy
npx wrangler secret put PROXY_URL_SECRET
npx wrangler secret put DISPATCHARR_PROXY_SECRET
npx wrangler secret put UPSTREAM_M3U_URL
npx wrangler secret put UPSTREAM_EPG_URL
```

`PROXY_URL_SECRET` should be a long random value. It encrypts rewritten stream/logo target URLs so users cannot decode the upstream URLs from the playlist. Keep the upstream playlist and EPG URLs in secrets only; do not commit them.

`DISPATCHARR_PROXY_SECRET` is sent upstream as `X-PocketTV-Proxy-Secret`. Use the same value in a Cloudflare WAF Skip rule on the Dispatcharr zone so the PocketTV proxy can reach the upstream playlist and guide even if a country block is enabled.

## Deploy

Deploy all three Workers:

```sh
cd workers/landing
npx wrangler deploy

cd ../signup-api
npx wrangler deploy

cd ../premium-proxy
npx wrangler deploy
```

Wrangler will attach the custom domains configured in each `wrangler.jsonc`.

## Smoke Tests

```sh
curl -fsS https://watch.pocket-tv.net/
curl -fsS https://api.pocket-tv.net/health
curl -fsS https://stream.pocket-tv.net/health
```

The proxy M3U and EPG endpoints should return `401 Unauthorized` unless a short-lived premium session token is supplied.

## Next Production Step

The proxy validates short-lived sessions stored in `ACTIVATION_CODES` and encrypts rewritten target URLs. Stripe webhooks create activation codes for successful checkouts and update code status when subscriptions change.
