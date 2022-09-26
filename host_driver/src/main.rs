use std::env;
use synergy_parser::SynergyClient;

#[tokio::main]
async fn main() {
    let width = env::var("BARRIER_SCREEN_WIDTH")
        .ok()
        .and_then(|w| w.parse().ok())
        .unwrap_or(2560);
    let height = env::var("BARRIER_SCREEN_HEIGHT")
        .ok()
        .and_then(|h| h.parse().ok())
        .unwrap_or(1600);
    let _conn = SynergyClient::connect(
        &env::var("BARRIER_SERVER").unwrap_or_else(|_| "localhost:24800".to_owned()),
        &env::var("KVM_SERIAL_ADDRESS").unwrap_or_else(|_| "/dev/ttyUSB0".to_owned()),
        env::var("KVM_SERIAL_BAUD")
            .ok()
            .and_then(|baud| baud.parse().ok())
            .unwrap_or(460800),
        &env::var("BARRIER_DEVICE_NAME").unwrap_or_else(|_| "Hardware Barrier".to_owned()),
        (width, height),
    )
    .await
    .unwrap();
}
