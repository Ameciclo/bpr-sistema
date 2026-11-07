/*
 * BPR Sistema — Reconstrução temporal de viagens (patch de horários)
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

// Patch de horários (melhorias/patch_bpr_horarios_viagens.md): a duração
// vem das horas CIVIS de saída e chegada; os pontos intermediários são
// reconstruídos por referência do MESMO trecho (contador); trechos sem
// referência ficam com hora estimada/desconhecida e método explícito —
// nunca inventada. Puro/testável (H01..H16).

/** Evento confirmado de saída/chegada (fato imutável) */
export interface TripEventFact {
  event_id: number;
  kind: "departure" | "arrival";
  base_id: string;
  civil_s: number; // UTC SEGUNDOS (firmware); 0 = sem hora válida
  civil_valid: boolean;
  counter_ms: number; // leitura local no evento
  boot_id: number; // trecho do contador
  method: string;
}

/** Referência de hora guardada pela bike (§5) */
export interface TimeRefFact {
  ref_id: number;
  civil_s: number; // UTC SEGUNDOS
  civil_valid: boolean;
  counter_ms: number;
  boot_id: number;
}

/** Registro de viagem publicado pela central */
export interface TripRecord {
  v: number;
  bike_id: string;
  trip_seq: number;
  events?: TripEventFact[];
  time_refs?: TimeRefFact[];
  sessions?: {session_id: number; session_salt: number}[];
  planned_sleep_sec?: number;
  sleep_nature?: string;
}

/** Reconstrução da viagem (separada dos fatos — §7) */
export interface TripReconstruction {
  v: number;
  trip_id: string;
  bike_id: string;
  departure: {utc_ms: number | null; valid: boolean};
  arrival: {utc_ms: number | null; valid: boolean};
  duration_s: number | null;
  method: string;
  quality: "ok" | "pending";
  pending_reason?: string;
}

/**
 * Reconstrói a viagem a partir dos FATOS. Duração = chegada − saída em
 * horas civis válidas (H01/H03/H04); sem hora válida ou com relógio
 * inconsistente, a pendência é explícita — sem duração negativa, falsa
 * precisão ou descarte (§7/H10).
 * @param {TripRecord} trip Registro factual da viagem
 * @return {TripReconstruction} reconstrução versionada
 */
export function reconstructTrip(trip: TripRecord): TripReconstruction {
  const tripId = `${trip.bike_id}#${trip.trip_seq}`;
  const events = trip.events ?? [];
  const dep = events.find((e) => e.kind === "departure");
  const arr = events.find((e) => e.kind === "arrival");

  const depUtc = dep && dep.civil_valid && dep.civil_s > 0 ?
    dep.civil_s * 1000 : null;
  const arrUtc = arr && arr.civil_valid && arr.civil_s > 0 ?
    arr.civil_s * 1000 : null;

  const base: TripReconstruction = {
    v: 1,
    trip_id: tripId,
    bike_id: trip.bike_id,
    departure: {utc_ms: depUtc, valid: depUtc !== null},
    arrival: {utc_ms: arrUtc, valid: arrUtc !== null},
    duration_s: null,
    method: "civil_diff_v1",
    quality: "pending",
  };

  if (depUtc === null && arrUtc === null) {
    return {...base, pending_reason: "no_valid_civil_reference"};
  }
  if (depUtc === null) {
    return {...base, pending_reason: "departure_without_valid_time"};
  }
  if (arrUtc === null) {
    return {...base, pending_reason: "awaiting_arrival"};
  }
  if (arrUtc < depUtc) {
    // Relógio inconsistente: registra a divergência, não força igualdade
    return {...base, pending_reason: "non_monotonic_civil_clock"};
  }
  return {
    ...base,
    duration_s: Math.round((arrUtc - depUtc) / 1000),
    quality: "ok",
  };
}

/** Observação factual (da projeção v2) para a timeline */
export interface TimelineScan {
  scan_key: string;
  boot_id: number;
  ts_ms: number; // contador do trecho
}

/** Ponto reconstruído (separado dos fatos) */
export interface TimelinePoint {
  utc_ms: number | null;
  basis: "ref_forward" | "arrival_backward" | "none";
  quality: "ok" | "ref_invalid" | "unknown";
}

/** Referências efetivas: time_refs + eventos válidos como âncoras (§3)
 * @param {object} trip Registro factual da viagem
 * @return {Array} referências por trecho (time_refs + eventos)
 */
function effectiveRefs(trip: TripRecord): TimeRefFact[] {
  const refs: TimeRefFact[] = [...(trip.time_refs ?? [])];
  for (const e of trip.events ?? []) {
    if (e.civil_valid && e.civil_s > 0) {
      refs.push({
        ref_id: e.event_id,
        civil_s: e.civil_s,
        civil_valid: true,
        counter_ms: e.counter_ms,
        boot_id: e.boot_id,
      });
    }
  }
  return refs;
}

/**
 * Reconstrói a hora de cada observação pela referência do SEU trecho
 * (H01/H02/H06): forward da referência (hora + (contador − contador_ref));
 * a CHEGADA também entra como referência efetiva do seu trecho (a "conta
 * para trás" do §3 é algebricamente esta mesma); sem nada, hora
 * desconhecida com método explícito (H08). Nunca aplica referência de
 * outro trecho (§6).
 * @param {Array} scans Observações com boot/trecho e contador
 * @param {object} trip Fatos da viagem (refs + eventos)
 * @return {object} ponto por scan_key
 */
export function buildTimeline(
  scans: TimelineScan[],
  trip: TripRecord
): Record<string, TimelinePoint> {
  const refs = effectiveRefs(trip);
  const points: Record<string, TimelinePoint> = {};

  for (const scan of scans) {
    const ref = refs.find((r) =>
      r.boot_id === scan.boot_id && r.civil_valid);
    if (ref) {
      // Hora do ponto = hora da referência + (contador do ponto − contador
      // na referência) — mesmo domínio de contador. A CHEGADA também entra
      // como referência efetiva do seu trecho (a "conta para trás" do §3 é
      // algebricamente esta mesma: civil − (counter_ref − ts))
      const utc = ref.civil_s * 1000 + (scan.ts_ms - ref.counter_ms);
      points[scan.scan_key] =
        {utc_ms: utc, basis: "ref_forward", quality: "ok"};
      continue;
    }
    // Sem referência suficiente para este trecho (H08): sequência mantida,
    // hora estimada/desconhecida com método explícito — sem invenção
    points[scan.scan_key] = {utc_ms: null, basis: "none", quality: "unknown"};
  }
  return points;
}
