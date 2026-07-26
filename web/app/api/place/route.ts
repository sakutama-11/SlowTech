import { kv } from "@vercel/kv";
import { NextResponse } from "next/server";

type PlaceEvent = {
  id: string;
  placedBy: string;
  placedAt: string;
  action: "placed" | "removed";
};

const LATEST_KEY = "yohako:place:latest";
const HISTORY_KEY = "yohako:place:history";

export async function GET() {
  const latest = (await kv.get<PlaceEvent>(LATEST_KEY)) ?? null;
  return NextResponse.json({ latest });
}

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as { placedBy?: string; action?: "placed" | "removed" };
    const placedBy = body.placedBy?.trim();
    const action = body.action;

    if (!placedBy) {
      return NextResponse.json({ error: "placedBy is required" }, { status: 400 });
    }

    if (action !== "placed" && action !== "removed") {
      return NextResponse.json({ error: "action must be placed or removed" }, { status: 400 });
    }

    const event: PlaceEvent = {
      id: crypto.randomUUID(),
      placedBy,
      placedAt: new Date().toISOString(),
      action
    };

    await kv.set(LATEST_KEY, event);
    await kv.lpush(HISTORY_KEY, event);
    await kv.ltrim(HISTORY_KEY, 0, 99);

    return NextResponse.json({ ok: true, latest: event });
  } catch {
    return NextResponse.json({ error: "invalid request" }, { status: 400 });
  }
}