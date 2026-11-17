pub fn kind(id: &[u8]) -> i32 {
    match id {
        b"RemoteMouseResetTask" => 0,
        b"AutoCombatTask" => 1,
        b"MultiAccountDailyTask" => 2,
        b"DailyTask" | b"DailyTask2" => 3,
        b"FarmEchoTask" | b"FarmEchoTask2" => 4,
        b"NightmareNestTask" => 5,
        _ if id.starts_with(b"TacetTask") => 6,
        _ if id.starts_with(b"ForgeryTask") => 7,
        _ if id.starts_with(b"SimulationTask") => 8,
        b"GardenTask" => 9,
        b"MergeEchoTask" => 10,
        b"EnhanceEchoTask" => 11,
        b"ChangeEchoTask" => 12,
        _ => -1,
    }
}
