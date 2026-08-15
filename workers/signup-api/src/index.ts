interface Env {
  SERVICE_NAME: string;
  STRIPE_SECRET_KEY: string;
  STRIPE_MONTHLY_PRICE_ID: string;
  SUCCESS_URL: string;
  CANCEL_URL: string;
  PREMIUM_PROXY_BASE_URL: string;
  ACTIVATION_CODES: KVNamespace;
}

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "Content-Type",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "X-Content-Type-Options": "nosniff"
};

class HttpError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      if (request.method === "OPTIONS") return new Response(null, { headers: CORS_HEADERS });

      const url = new URL(request.url);
      if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/health")) {
        return json({ ok: true, service: env.SERVICE_NAME });
      }

      if (request.method === "POST" && url.pathname === "/api/create-checkout-session") {
        return createCheckoutSession(request, env);
      }
      if (request.method === "POST" && url.pathname === "/api/redeem-code") {
        return redeemCode(request, env);
      }

      throw new HttpError(404, "Not found");
    } catch (error) {
      if (error instanceof HttpError) return json({ error: error.message }, error.status);
      console.error(error);
      return json({ error: "Internal server error" }, 500);
    }
  }
};

async function createCheckoutSession(request: Request, env: Env): Promise<Response> {
  const body = await parseJson(request);
  if (body.plan !== "pockettv-premium-monthly") throw new HttpError(400, "Unknown plan");
  requireEnv(env.STRIPE_SECRET_KEY, "STRIPE_SECRET_KEY");
  requireEnv(env.STRIPE_MONTHLY_PRICE_ID, "STRIPE_MONTHLY_PRICE_ID");
  requireEnv(env.SUCCESS_URL, "SUCCESS_URL");
  requireEnv(env.CANCEL_URL, "CANCEL_URL");

  const params = new URLSearchParams();
  params.set("mode", "subscription");
  params.set("line_items[0][price]", env.STRIPE_MONTHLY_PRICE_ID);
  params.set("line_items[0][quantity]", "1");
  params.set("subscription_data[trial_period_days]", "7");
  params.set("allow_promotion_codes", "true");
  params.set("billing_address_collection", "auto");
  params.set("success_url", env.SUCCESS_URL);
  params.set("cancel_url", env.CANCEL_URL);
  params.set("metadata[plan]", "pockettv-premium-monthly");

  const stripeResponse = await fetch("https://api.stripe.com/v1/checkout/sessions", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${env.STRIPE_SECRET_KEY}`,
      "Content-Type": "application/x-www-form-urlencoded"
    },
    body: params
  });

  const payload = (await stripeResponse.json()) as StripeCheckoutSession | StripeError;
  if (!stripeResponse.ok || !("url" in payload) || !payload.url) {
    const message = "error" in payload ? payload.error.message : "Stripe checkout failed";
    throw new HttpError(502, message);
  }

  return json({ url: payload.url });
}

async function redeemCode(request: Request, env: Env): Promise<Response> {
  const body = await parseJson(request);
  const code = normalizeActivationCode(body.code);
  if (!code) throw new HttpError(400, "Enter an 8-character activation code");
  requireEnv(env.PREMIUM_PROXY_BASE_URL, "PREMIUM_PROXY_BASE_URL");

  const record = await env.ACTIVATION_CODES.get<ActivationCodeRecord>(`code:${code}`, "json");
  if (!record || record.status !== "active") throw new HttpError(404, "Activation code not found");

  const proxyBase = env.PREMIUM_PROXY_BASE_URL.replace(/\/+$/g, "");
  return json({
    status: "active",
    m3uUrl: `${proxyBase}/premium/m3u?token=${encodeURIComponent(record.proxyToken)}`,
    epgUrl: `${proxyBase}/premium/epg?token=${encodeURIComponent(record.proxyToken)}`
  });
}

async function parseJson(request: Request): Promise<{ plan?: string; code?: string }> {
  try {
    return await request.json();
  } catch {
    throw new HttpError(400, "Invalid JSON");
  }
}

function normalizeActivationCode(value: string | undefined): string {
  if (!value) return "";
  const code = value.toUpperCase().replace(/[^A-Z0-9]/g, "");
  return /^[A-Z0-9]{8}$/.test(code) ? code : "";
}

function requireEnv(value: string | undefined, name: string): void {
  if (!value) throw new HttpError(500, `${name} is not configured`);
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...CORS_HEADERS,
      "Content-Type": "application/json; charset=utf-8"
    }
  });
}

interface StripeCheckoutSession {
  id: string;
  url: string | null;
}

interface StripeError {
  error: {
    message: string;
  };
}

interface ActivationCodeRecord {
  status: "active" | "canceled" | "past_due";
  proxyToken: string;
  stripeCustomerId?: string;
  stripeSubscriptionId?: string;
}

interface KVNamespace {
  get<T = string>(key: string, type: "json"): Promise<T | null>;
  get(key: string): Promise<string | null>;
}
