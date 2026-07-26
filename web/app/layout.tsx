import "./globals.css";
import type { Metadata } from "next";

export const metadata: Metadata = {
  title: "Yohakō - 余白の時間を想う",
  description: "BLEで香りデータを送信し、誰が置いたかを記録するYohakōアプリ"
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="ja">
      <body>{children}</body>
    </html>
  );
}