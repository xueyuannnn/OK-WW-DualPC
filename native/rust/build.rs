use std::{env, fs, path::PathBuf};

fn generate(file: &str, prefix: &str, target: &str) {
    println!("cargo:rerun-if-changed={file}");
    let source = fs::read_to_string(file).expect("ABI definition");
    let mut ops = Vec::new();
    let mut methods = Vec::new();
    for line in source.lines().map(str::trim) {
        if line.is_empty() || line.starts_with("//") {
            continue;
        }
        let (kind, fields) = line.split_once('(').expect("ABI entry");
        let fields: Vec<_> = fields
            .strip_suffix(')')
            .expect("ABI closing delimiter")
            .split(',')
            .map(str::trim)
            .collect();
        match kind {
            kind if kind == format!("{prefix}_OP") && fields.len() == 2 => {
                ops.push((fields[0], fields[1].parse::<i32>().expect("op ID")))
            }
            kind if kind == format!("{prefix}_METHOD") && fields.len() == 3 => methods.push((
                fields[0],
                fields[1].parse::<i32>().expect("method ID"),
                fields[2].parse::<usize>().expect("argument count"),
            )),
            _ => panic!("Unknown ABI entry: {line}"),
        }
    }
    let mut output = String::from("#[repr(i32)]\n#[derive(Clone,Copy)]\npub enum Op {\n");
    for (name, id) in ops {
        output += &format!("{name}={id},\n");
    }
    output += "}\n#[repr(i32)]\n#[derive(Clone,Copy)]\npub enum Method {\n";
    for (name, id, _) in &methods {
        output += &format!("{name}={id},\n");
    }
    output += "}\nimpl TryFrom<i32> for Method {type Error=i32;fn try_from(value:i32)->std::result::Result<Self,i32>{match value{\n";
    for (name, id, _) in &methods {
        output += &format!("{id}=>Ok(Self::{name}),\n");
    }
    output += "_=>Err(INVALID_ACTION),}}}\nimpl Method {fn arg_count(self)->usize {match self {\n";
    for (name, _, count) in &methods {
        output += &format!("Self::{name}=>{count},\n");
    }
    output += "}}}\n";
    fs::write(
        PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR")).join(target),
        output,
    )
    .expect("write ABI bindings");
}
fn main() {
    generate("abi/rotation.def", "OKWW_ROTATION", "rotation_abi.rs");
    generate("abi/taskflow.def", "OKWW_TASKFLOW", "taskflow_abi.rs");
    generate("abi/engine.def", "OKWW_ENGINE", "engine_abi.rs");
    generate("abi/session.def", "OKWW_SESSION", "session_abi.rs");
    generate("abi/visionflow.def", "OKWW_VISION", "visionflow_abi.rs");
    generate("abi/workflow.def", "OKWW_WORKFLOW", "workflow_abi.rs");
    generate_session_fields();
    generate_commands();
}

fn generate_session_fields() {
    let file = "abi/session_state.def";
    println!("cargo:rerun-if-changed={file}");
    let source = fs::read_to_string(file).expect("session state definitions");
    let mut output = String::new();
    for (kind, name) in [("BOOL", "Bool"), ("TIME", "Time"), ("COUNT", "Count")] {
        output += &format!("#[repr(i32)]\n#[derive(Clone,Copy)]\npub enum {name} {{\n");
        let prefix = format!("OKWW_SESSION_{kind}(");
        for line in source.lines().map(str::trim) {
            if let Some(fields) = line.strip_prefix(&prefix) {
                let fields: Vec<_> = fields
                    .strip_suffix(')')
                    .expect("state delimiter")
                    .split(',')
                    .map(str::trim)
                    .collect();
                assert_eq!(fields.len(), 3);
                let id = fields[1].parse::<i32>().expect("state ID");
                output += &format!("{}={id},\n", fields[0]);
            }
        }
        output += "}\n";
    }
    fs::write(
        PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR")).join("session_state.rs"),
        output,
    )
    .expect("write session state");
}

fn generate_commands() {
    let file = "abi/commands.def";
    println!("cargo:rerun-if-changed={file}");
    let source = fs::read_to_string(file).expect("command ABI definition");
    let entries: Vec<_> = source
        .lines()
        .filter(|line| !line.trim().is_empty())
        .map(|line| {
            let fields: Vec<_> = line
                .trim()
                .strip_prefix("OKWW_COMMAND(")
                .expect("command entry")
                .strip_suffix(')')
                .expect("command delimiter")
                .split(',')
                .map(str::trim)
                .collect();
            assert_eq!(fields.len(), 3);
            (
                fields[0],
                fields[1].parse::<i32>().expect("command ID"),
                fields[2],
            )
        })
        .collect();
    let mut output = String::from(
        "#[repr(i32)]\n#[derive(Clone,Copy,PartialEq,Eq,Debug)]\npub enum Command {\n",
    );
    for (name, id, _) in &entries {
        output += &format!("{name}={id},\n");
    }
    output += "Script=18,Team=19,Unknown=20,}\nimpl Command { pub fn parse(value:&[u8])->Self { match value {\n";
    for (name, _, text) in &entries {
        output += &format!("b\"{text}\"=>Self::{name},\n");
    }
    output += "v if v.starts_with(b\"script_\")=>Self::Script,v if v.starts_with(b\"team_\")=>Self::Team,_=>Self::Unknown,}}}\n";
    output += "impl TryFrom<i32> for Command {type Error=i32;fn try_from(value:i32)->Result<Self,i32>{match value{\n";
    for (name, id, _) in &entries {
        output += &format!("{id}=>Ok(Self::{name}),\n");
    }
    output += "18=>Ok(Self::Script),19=>Ok(Self::Team),20=>Ok(Self::Unknown),_=>Err(INVALID_ACTION),}}}\n";
    fs::write(
        PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR")).join("commands_abi.rs"),
        output,
    )
    .expect("write command bindings");
}
