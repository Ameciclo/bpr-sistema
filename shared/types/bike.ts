// Tipos compartilhados para o sistema BPR

export interface BikeStatus {
  id: string
  lastUpdate: number
  battery: number
  isCharging: boolean
  location?: {
    lat: number
    lng: number
  }
  status: 'active' | 'inactive' | 'charging' | 'maintenance'
}

export interface WiFiNetwork {
  ssid: string
  bssid: string
  rssi: number
  channel: number
}

export interface ScanData {
  timestamp: number
  realTime: number
  battery: number
  networks: WiFiNetwork[]
}

export interface BikeSession {
  start: number
  end: number
  mode: 'normal' | 'charging'
  scans: ScanData[]
}

export interface BikeConfig {
  id: string
  scanTimeActive: number
  scanTimeInactive: number
  bases: Array<{
    ssid: string
    password: string
  }>
  firebase: {
    url: string
    key: string
  }
}