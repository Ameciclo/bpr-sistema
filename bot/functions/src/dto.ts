/*
 * BPR Sistema — DTO interno de rede WiFi (fonte única do contrato)
 * Copyright (C) 2024 BPR Sistema Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

// A02: DTO interno de rede — único formato aceito por produtores/consumidores.
// Extraído de index.ts para ser compartilhado com assembly.ts (testável).

/** Rede WiFi no formato interno canônico */
export interface NetworkDto {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
}

/**
 * Normaliza uma rede WiFi para o DTO interno.
 * @param {unknown} net Rede no formato array do firmware ou objeto v2
 * @return {NetworkDto} DTO normalizado
 */
export function normalizeNetwork(net: unknown): NetworkDto {
  if (Array.isArray(net)) {
    return {
      ssid: String(net[0] ?? ""),
      bssid: String(net[1] ?? ""),
      rssi: Number(net[2] ?? 0),
      channel: Number(net[3] ?? 0),
    };
  }
  if (net && typeof net === "object") {
    const o = net as Record<string, unknown>;
    return {
      ssid: String(o.ssid ?? ""),
      bssid: String(o.bssid ?? ""),
      rssi: Number(o.rssi ?? 0),
      channel: Number(o.channel ?? 0),
    };
  }
  return {ssid: "", bssid: "", rssi: 0, channel: 0};
}
