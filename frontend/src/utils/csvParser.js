// Utility to parse CSV into array of numerical objects for Recharts
export function parseCSV(csvText) {
  if (!csvText || typeof csvText !== 'string') return [];
  const lines = csvText.trim().split(/\r?\n/);
  if (lines.length < 2) return [];

  const headers = lines[0].split(',').map(h => h.trim());
  const rows = [];

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;
    const values = line.split(',');
    const row = {};
    headers.forEach((h, idx) => {
      const val = values[idx] !== undefined ? values[idx].trim() : '';
      const num = parseFloat(val);
      row[h] = isNaN(num) || val === '' ? val : num;
    });
    rows.push(row);
  }
  return rows;
}

export function parseSummaryCSV(summaryText) {
  if (!summaryText) return {};
  const rows = parseCSV(summaryText);
  const obj = {};
  rows.forEach(r => {
    if (r.metric !== undefined) {
      obj[r.metric] = r.value;
    }
  });
  return obj;
}
