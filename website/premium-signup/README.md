# PocketTV Premium Signup Site

Static signup page for PocketTV Premium.

## Plan

- Free trial: 7 days
- Recurring price: $9.99/month
- Payment handoff: Stripe Checkout subscription mode

Stripe Checkout should create a subscription session with:

```json
{
  "mode": "subscription",
  "line_items": [{ "price": "STRIPE_MONTHLY_PRICE_ID", "quantity": 1 }],
  "subscription_data": {
    "trial_period_days": 7
  },
  "success_url": "https://YOUR_SITE/setup-success?session_id={CHECKOUT_SESSION_ID}",
  "cancel_url": "https://YOUR_SITE/"
}
```

Stripe's Checkout Session API supports subscription trial days through `subscription_data.trial_period_days`.

## Frontend Contract

The button calls:

```http
POST /api/create-checkout-session
Content-Type: application/json

{ "plan": "pockettv-premium-monthly" }
```

Expected response:

```json
{ "url": "https://checkout.stripe.com/c/..." }
```

The matching Cloudflare Worker API lives in `workers/signup-api`.

## Deploy The Checkout API

Create a Stripe product/price for `$9.99` monthly billing, then set:

```bash
cd workers/signup-api
npm install
npx wrangler secret put STRIPE_SECRET_KEY
npx wrangler secret put STRIPE_MONTHLY_PRICE_ID
npx wrangler secret put SUCCESS_URL
npx wrangler secret put CANCEL_URL
npm run deploy
```

`SUCCESS_URL` should point to the page that shows the customer's generated PocketTV Premium M3U/EPG proxy URLs.

`CANCEL_URL` should point back to this signup page.

## Subscription Fulfillment

After Stripe confirms the subscription:

1. Create or update the customer account.
2. Generate a PocketTV Premium token.
3. Store that token with subscription status.
4. Show the user their proxy-backed M3U and EPG URLs:

```text
https://YOUR_WORKER_DOMAIN/premium/m3u?token=USER_TOKEN
https://YOUR_WORKER_DOMAIN/premium/epg?token=USER_TOKEN
```

The premium proxy Worker is in `workers/premium-proxy`.

## Local Preview

Open `index.html` directly in a browser, or serve this folder with any static web server.
