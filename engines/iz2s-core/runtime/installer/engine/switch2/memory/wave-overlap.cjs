// Qualify the request lifetimes observed in one gap-probe wave.
const HELD_ROLES = ['canary', 'filler0', 'filler1', 'guard0', 'guard1'];
const THROUGH_ROLES = ['commit', 'tail0', 'tail1', 'tail2', 'tail3'];

function qualifyOverlap(rows) {
  const byRole = new Map(rows.map(row => [row.role, row]));
  const trigger = byRole.get('trigger');
  const held = HELD_ROLES.map(role => byRole.get(role));
  const through = THROUGH_ROLES.map(role => byRole.get(role));
  const mode = 'trigger-entry';
  if (!trigger || held.some(row => !row) || through.some(row => !row))
    return {qualified: false, mode};

  const startedBeforeTrigger = [...held, ...through].every(row => row.startUs < trigger.startUs);
  const throughTriggerEnd = through.every(row => trigger.endUs < row.endUs);
  const heldLongEnough = held.every(row => trigger.startUs < row.endUs);
  return {qualified: startedBeforeTrigger && throughTriggerEnd && heldLongEnough, mode};
}

module.exports = {qualifyOverlap};
