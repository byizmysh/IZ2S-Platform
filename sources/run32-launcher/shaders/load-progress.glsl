// Dedicated mailbox in the font gap, disjoint from activity and notifications.
const uint LOAD_PROGRESS=0x120000u/4u;
bool loadProgressVisible(){return c.atlas[LOAD_PROGRESS]==0x495a5031u&&c.atlas[LOAD_PROGRESS+1u]==1u&&c.atlas[LOAD_PROGRESS+2u]>=1u&&c.atlas[LOAD_PROGRESS+2u]<=7u;}
bool loadProgressBusy(){return loadProgressVisible()&&c.atlas[LOAD_PROGRESS+2u]<=5u;}
