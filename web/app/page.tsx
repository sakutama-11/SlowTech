"use client";

import { useEffect, useMemo, useRef, useState } from "react";

type PlaceEvent = {
  id: string;
  placedBy: string;
  placedAt: string;
  action: "placed" | "removed";
};

const SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214";
const CHARACTERISTIC_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214";
const PRESENCE_CHARACTERISTIC_UUID = "19b10002-e8f2-537e-4f6c-d104768a1214";

export default function Page() {
  const [placedBy, setPlacedBy] = useState("");
  const [status, setStatus] = useState("ボタンを押してデバイスと接続してください");
  const [isConnecting, setIsConnecting] = useState(false);
  const [isConnected, setIsConnected] = useState(false);
  const [bleCharacteristic, setBleCharacteristic] = useState<BluetoothRemoteGATTCharacteristic | null>(null);
  const [latest, setLatest] = useState<PlaceEvent | null>(null);
  const deviceRef = useRef<BluetoothDevice | null>(null);
  const presenceCharacteristicRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const controlCharacteristicRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const lastPresenceValueRef = useRef<number | null>(null);
  const nameRef = useRef("");

  const canStart = useMemo(() => placedBy.trim().length > 0 && !isConnecting, [placedBy, isConnecting]);

  useEffect(() => {
    void fetchLatest();
  }, []);

  useEffect(() => {
    nameRef.current = placedBy.trim();
  }, [placedBy]);

  async function fetchLatest() {
    const response = await fetch("/api/place", { method: "GET", cache: "no-store" });
    if (!response.ok) return;
    const data = (await response.json()) as { latest: PlaceEvent | null };
    setLatest(data.latest);
  }

  async function connectToDevice() {
    if (!placedBy.trim()) {
      setStatus("先に『誰が置いたか』を入力してください");
      return;
    }

    setIsConnecting(true);
    setStatus("デバイスを探しています...");

    try {
      const device = await navigator.bluetooth.requestDevice({
        filters: [{ name: "Yohako-01" }],
        optionalServices: [SERVICE_UUID]
      });

      setStatus("接続中...");

      const server = await device.gatt?.connect();
      if (!server) throw new Error("GATT接続に失敗しました");

      const service = await server.getPrimaryService(SERVICE_UUID);
      const characteristic = await service.getCharacteristic(CHARACTERISTIC_UUID);
      const presenceCharacteristic = await service.getCharacteristic(PRESENCE_CHARACTERISTIC_UUID);

      setBleCharacteristic(characteristic);
      setIsConnected(true);
      deviceRef.current = device;
      presenceCharacteristicRef.current = presenceCharacteristic;
      controlCharacteristicRef.current = characteristic;

      await sendScentData(characteristic);
      await startPresenceNotifications(presenceCharacteristic);
      await fetchLatest();

      setStatus("接続成功。香りデータ送信済み。センサー通知待機中です。");
    } catch (error) {
      console.error(error);
      setStatus("接続または送信に失敗しました。再試行してください。");
    } finally {
      setIsConnecting(false);
    }
  }

  async function disconnectDevice() {
    const presenceCharacteristic = presenceCharacteristicRef.current;
    if (presenceCharacteristic) {
      presenceCharacteristic.removeEventListener("characteristicvaluechanged", onPresenceChanged);
      await presenceCharacteristic.stopNotifications();
    }

    if (bleCharacteristic) {
      await sendStopCommand(bleCharacteristic);
    }

    const device = deviceRef.current;
    if (device?.gatt?.connected) {
      device.gatt.disconnect();
    }

    presenceCharacteristicRef.current = null;
    controlCharacteristicRef.current = null;
    deviceRef.current = null;
    lastPresenceValueRef.current = null;
    setBleCharacteristic(null);
    setIsConnected(false);
    setStatus("停止コマンドを送信し、切断しました。");
  }

  async function startPresenceNotifications(characteristic: BluetoothRemoteGATTCharacteristic) {
    await characteristic.startNotifications();
    characteristic.addEventListener("characteristicvaluechanged", onPresenceChanged);
  }

  function onPresenceChanged(event: Event) {
    const target = event.target as BluetoothRemoteGATTCharacteristic;
    const value = target.value?.getUint8(0);
    if (value === undefined) {
      return;
    }

    if (lastPresenceValueRef.current === value) {
      return;
    }
    lastPresenceValueRef.current = value;

    void handlePresenceEvent(value);
  }

  async function handlePresenceEvent(value: number) {
    const action: "placed" | "removed" = value === 1 ? "placed" : "removed";
    const actor = nameRef.current || "不明";

    if (action === "placed") {
      setStatus("フォトリフレクタ検知: 置いた");
      const characteristic = controlCharacteristicRef.current;
      if (characteristic) {
        await sendScentData(characteristic);
      }
    } else {
      setStatus("フォトリフレクタ検知: 外した");
      const characteristic = controlCharacteristicRef.current;
      if (characteristic) {
        await sendStopCommand(characteristic);
      }
    }

    await savePresenceEvent(actor, action);
    await fetchLatest();
  }

  async function sendScentData(characteristic: BluetoothRemoteGATTCharacteristic) {
    const data = new Uint8Array([255, 128, 0]);
    await characteristic.writeValue(data);
  }

  async function sendStopCommand(characteristic: BluetoothRemoteGATTCharacteristic) {
    const stopData = new Uint8Array([0, 0, 0]);
    await characteristic.writeValue(stopData);
  }

  async function savePresenceEvent(name: string, action: "placed" | "removed") {
    const response = await fetch("/api/place", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ placedBy: name, action })
    });

    if (!response.ok) {
      throw new Error("置いた人の記録に失敗しました");
    }
  }

  return (
    <main>
      <section className="card">
        <h1 className="title">Yohakō</h1>
        <p className="subtitle">誰かを想う、余白の時間</p>

        <div className="row">
          <label htmlFor="placedBy">誰が置いたか</label>
          <input
            id="placedBy"
            placeholder="例: Aさん"
            value={placedBy}
            onChange={(e) => setPlacedBy(e.target.value)}
            maxLength={40}
          />
        </div>

        <div className="row">
          <button className="primary" onClick={connectToDevice} disabled={!canStart || isConnected}>
            余白の時間を始める
          </button>
          <button className="secondary" onClick={disconnectDevice} disabled={!isConnected}>
            接続を切断
          </button>
        </div>

        <div className="status">{status}</div>

        <div className="latest">
          {latest ? (
            <>
              <strong>最新の記録:</strong> {latest.placedBy} が {latest.action === "placed" ? "置いた" : "外した"} / {new Date(latest.placedAt).toLocaleString("ja-JP")}
            </>
          ) : (
            <>最新の記録: まだありません</>
          )}
        </div>
      </section>
    </main>
  );
}