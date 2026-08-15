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
npx wrangler secret put PREMIUM_PROXY_BASE_URL
npm run deploy
```

`SUCCESS_URL` should point to the page that shows the customer's generated 8-character PocketTV activation code.

`CANCEL_URL` should point back to this signup page.

Create the `ACTIVATION_CODES` KV namespace and replace `REPLACE_WITH_KV_NAMESPACE_ID` in `workers/signup-api/wrangler.jsonc`.

## Subscription Fulfillment

After Stripe confirms the subscription:

1. Create or update the customer account.
2. Generate a random 8-character activation code, using uppercase letters and digits.
3. Generate a long random proxy token for that customer.
4. Store the activation record in the `ACTIVATION_CODES` KV namespace:

```json
{
  "status": "active",
  "proxyToken": "LONG_RANDOM_PROXY_TOKEN",
  "stripeCustomerId": "cus_...",
  "stripeSubscriptionId": "sub_..."
}
```

Use the key format:

```text
code:PK7X2Q9A
```

5. Show only the 8-character activation code to the customer.

## App Redemption Contract

PocketTV should call:

```http
POST /api/redeem-code
Content-Type: application/json

{ "code": "PK7X2Q9A" }
```

Expected response:

```json
{
  "status": "active",
  "m3uUrl": "https://YOUR_WORKER_DOMAIN/premium/m3u?token=...",
  "epgUrl": "https://YOUR_WORKER_DOMAIN/premium/epg?token=..."
}
```

PocketTV can save those returned proxy URLs internally so the user never has to type M3U or EPG URLs.

The premium proxy Worker is in `workers/premium-proxy`.

## Local Preview

Open `index.html` directly in a browser, or serve this folder with any static web server.
