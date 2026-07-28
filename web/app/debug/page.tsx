'use client';

import { useState, useRef, useEffect } from 'react';

interface SensorLog {
  timestamp: number;
  timecode: string;
  state: 'placed' | 'removed';
}

export default function DebugPage() {
  const [connected, setConnected] = useState(false);
  const [fan1, setFan1] = useState(200);
  const [fan2, setFan2] = useState(200);
  const [fan3, setFan3] = useState(200);
  const [logs, setLogs] = useState<SensorLog[]>([]);
  const [statusMessage, setStatusMessage] = useState('');

  const deviceRef = useRef<BluetoothDevice | null>(null);
  const characteristicRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const startTimeRef = useRef<number>(0);

  // タイムコード生成（mm:ss.ms形式）
  const getTimecode = (ms: number): string => {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    const milliseconds = ms % 1000;
    return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}.${String(milliseconds).padStart(3, '0')}`;
  };

  // Arduino に接続
  const connectToDevice = async () => {
    try {
      setStatusMessage('接続中...');
      
      const device = await navigator.bluetooth.requestDevice({
        filters: [{ name: 'Yohako-01' }],
        optionalServices: ['19b10000-e8f2-537e-4f6c-d104768a1214'],
      });

      const server = await device.gatt?.connect();
      if (!server) {
        setStatusMessage('GATT接続失敗');
        return;
      }

      const service = await server.getPrimaryService('19b10000-e8f2-537e-4f6c-d104768a1214');
      const scentCharacteristic = await service.getCharacteristic('19b10001-e8f2-537e-4f6c-d104768a1214');
      const presenceCharacteristic = await service.getCharacteristic('19b10002-e8f2-537e-4f6c-d104768a1214');

      deviceRef.current = device;
      characteristicRef.current = scentCharacteristic;
      startTimeRef.current = Date.now();

      setConnected(true);
      setLogs([]);
      setStatusMessage('✓ 接続完了');

      // フォトセンサの変化を監視
      await presenceCharacteristic.startNotifications();
      presenceCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
        const target = event.target as BluetoothRemoteGATTCharacteristic;
        const value = target.value?.getUint8(0);
        const elapsed = Date.now() - startTimeRef.current;
        const timecode = getTimecode(elapsed);
        const state = value === 1 ? 'placed' : 'removed';

        const newLog: SensorLog = { timestamp: Date.now(), timecode, state };
        setLogs((prev) => [newLog, ...prev]);
        console.log(`[${timecode}] Sensor: ${state}`);
      });

      device.addEventListener('gattserverdisconnected', () => {
        setConnected(false);
        setStatusMessage('⚠ 接続切断');
      });
    } catch (error) {
      console.error('接続エラー:', error);
      setStatusMessage(`エラー: ${error}`);
      setConnected(false);
    }
  };

  // ファンデータを送信
  const sendFanData = async () => {
    if (!characteristicRef.current || !connected) {
      setStatusMessage('未接続です');
      return;
    }

    try {
      const data = new Uint8Array([fan1, fan2, fan3]);
      await characteristicRef.current.writeValue(data);
      setStatusMessage(`✓ 送信完了: Fan1=${fan1}, Fan2=${fan2}, Fan3=${fan3}`);
      console.log('送信:', data);
    } catch (error) {
      setStatusMessage(`送信エラー: ${error}`);
    }
  };

  // 停止コマンド送信（0,0,0）
  const sendStop = async () => {
    if (!characteristicRef.current || !connected) {
      setStatusMessage('未接続です');
      return;
    }

    try {
      const data = new Uint8Array([0, 0, 0]);
      await characteristicRef.current.writeValue(data);
      setStatusMessage('✓ 停止コマンド送信');
    } catch (error) {
      setStatusMessage(`送信エラー: ${error}`);
    }
  };

  // 接続を切断
  const disconnect = () => {
    if (deviceRef.current) {
      deviceRef.current.gatt?.disconnect();
      setConnected(false);
      setStatusMessage('切断しました');
    }
  };

  return (
    <div style={{ padding: '20px', fontFamily: 'sans-serif' }}>
      <h1>🔧 Yohakō デバッグコンソール</h1>

      {/* ステータス表示 */}
      <div
        style={{
          padding: '10px',
          marginBottom: '20px',
          backgroundColor: connected ? '#e8f5e9' : '#fff3e0',
          border: '1px solid #ccc',
          borderRadius: '4px',
        }}
      >
        <strong>ステータス:</strong> {connected ? '✓ 接続中' : '✗ 未接続'}
        <br />
        <small>{statusMessage}</small>
      </div>

      {/* 接続コントロール */}
      <div style={{ marginBottom: '20px' }}>
        <h2>接続</h2>
        <button
          onClick={connectToDevice}
          disabled={connected}
          style={{
            padding: '10px 20px',
            marginRight: '10px',
            backgroundColor: connected ? '#ccc' : '#4CAF50',
            color: 'white',
            border: 'none',
            borderRadius: '4px',
            cursor: connected ? 'not-allowed' : 'pointer',
          }}
        >
          Arduino に接続
        </button>
        <button
          onClick={disconnect}
          disabled={!connected}
          style={{
            padding: '10px 20px',
            backgroundColor: !connected ? '#ccc' : '#f44336',
            color: 'white',
            border: 'none',
            borderRadius: '4px',
            cursor: !connected ? 'not-allowed' : 'pointer',
          }}
        >
          切断
        </button>
      </div>

      {/* ファン制御 */}
      <div style={{ marginBottom: '20px' }}>
        <h2>ファン制御</h2>
        <div style={{ display: 'flex', gap: '20px', marginBottom: '10px', flexWrap: 'wrap' }}>
          <div>
            <label>Fan1 (0-255): </label>
            <input
              type="number"
              min="0"
              max="255"
              value={fan1}
              onChange={(e) => setFan1(Number(e.target.value))}
              disabled={!connected}
              style={{ width: '60px', padding: '5px' }}
            />
          </div>
          <div>
            <label>Fan2 (0-255): </label>
            <input
              type="number"
              min="0"
              max="255"
              value={fan2}
              onChange={(e) => setFan2(Number(e.target.value))}
              disabled={!connected}
              style={{ width: '60px', padding: '5px' }}
            />
          </div>
          <div>
            <label>Fan3 (0-255): </label>
            <input
              type="number"
              min="0"
              max="255"
              value={fan3}
              onChange={(e) => setFan3(Number(e.target.value))}
              disabled={!connected}
              style={{ width: '60px', padding: '5px' }}
            />
          </div>
        </div>
        <button
          onClick={sendFanData}
          disabled={!connected}
          style={{
            padding: '10px 20px',
            marginRight: '10px',
            backgroundColor: !connected ? '#ccc' : '#2196F3',
            color: 'white',
            border: 'none',
            borderRadius: '4px',
            cursor: !connected ? 'not-allowed' : 'pointer',
          }}
        >
          送信
        </button>
        <button
          onClick={sendStop}
          disabled={!connected}
          style={{
            padding: '10px 20px',
            backgroundColor: !connected ? '#ccc' : '#FF9800',
            color: 'white',
            border: 'none',
            borderRadius: '4px',
            cursor: !connected ? 'not-allowed' : 'pointer',
          }}
        >
          停止 (0,0,0)
        </button>
      </div>

      {/* センサログ */}
      <div>
        <h2>フォトセンサ監視ログ</h2>
        <div
          style={{
            border: '1px solid #ccc',
            borderRadius: '4px',
            padding: '10px',
            height: '300px',
            overflowY: 'auto',
            backgroundColor: '#f9f9f9',
            fontFamily: 'monospace',
            fontSize: '12px',
          }}
        >
          {logs.length === 0 ? (
            <div style={{ color: '#999' }}>ログなし</div>
          ) : (
            logs.map((log, idx) => (
              <div key={idx} style={{ marginBottom: '5px' }}>
                <span style={{ color: '#666' }}>[{log.timecode}]</span>{' '}
                <span style={{ color: log.state === 'placed' ? '#d32f2f' : '#388e3c', fontWeight: 'bold' }}>
                  {log.state === 'placed' ? '🔴 PLACED' : '🟢 REMOVED'}
                </span>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
