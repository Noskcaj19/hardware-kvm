mod packet_reader;

use crate::packet_reader::{PacketError, PacketReader, PacketWriter};
use byteorder_async::{BigEndian, WriteBytesExt};
use serialport::{DataBits, FlowControl, Parity, StopBits};
use std::collections::HashMap;
use std::io;
use std::time::Duration;
use thiserror::Error;
use tokio::io::{AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::net::TcpStream;

#[derive(Error, Debug)]
pub enum ConnectionError {
    #[error("tcp connection failed")]
    TcpError(#[from] io::Error),
    #[error("invalid data received")]
    ProtocolError(#[from] PacketError),
}

pub struct SynergyClient<S: PacketReader + PacketWriter> {
    _packet_stream: PacketStream<S>,
}

pub struct PacketStream<S: PacketReader + PacketWriter> {
    stream: S,
}

impl<S: PacketReader + PacketWriter> PacketStream<S> {
    pub async fn read(&mut self) -> Result<Packet, PacketError> {
        // let mut x = [0; 4];
        // self.stream.read(&mut x).await?;
        // let size = u32::from_be_bytes(x);
        let size = self.stream.read_packet_size().await?;
        if size < 4 {
            let mut vec = Vec::new();
            self.stream.read_to_end(&mut vec).await?;
            return Err(PacketError::PacketTooSmall);
        }
        let code: [u8; 4] = self.stream.read_bytes_fixed().await?;
        let code = String::from_utf8_lossy(&code).into_owned();

        let packet = match code.as_ref() {
            "QINF" => Packet::QueryInfo,
            "CIAK" => Packet::InfoAck,
            "CALV" => Packet::KeepAlive,
            "CROP" => Packet::ResetOptions,
            "DSOP" => {
                let num_items = self.stream.read_u32().await?;
                let num_opts = num_items / 2;
                let mut opts = HashMap::new();
                for _ in 0..num_opts {
                    let opt: [u8; 4] = self.stream.read_bytes_fixed().await?;
                    let opt = String::from_utf8_lossy(&opt).into_owned();
                    let val = self.stream.read_u32().await?;
                    tracing::debug!("Read option: {opt} with value {val:?}");
                    opts.insert(opt, val);
                }
                Packet::SetDeviceOptions(opts)
            }
            "EUNK" => Packet::ErrorUnknownDevice,
            "DMMV" => {
                let x = self.stream.read_u16().await?;
                let y = self.stream.read_u16().await?;
                Packet::MouseMoveAbs { x, y }
            }
            "CINN" => {
                let x = self.stream.read_u16().await?;
                let y = self.stream.read_u16().await?;
                let seq_num = self.stream.read_u32().await?;
                let mask = self.stream.read_u16().await?;
                Packet::CursorEnter {
                    x,
                    y,
                    seq_num,
                    mask,
                }
            }
            "COUT" => Packet::CursorLeave,
            "CCLP" => {
                let id = self.stream.read_u8().await?;
                let seq_num = self.stream.read_u32().await?;
                Packet::GrabClipboard { id, seq_num }
            }
            "DCLP" => {
                let id = self.stream.read_u8().await?;
                let seq_num = self.stream.read_u32().await?;
                let mark = self.stream.read_u8().await?;
                let data = self.stream.read_bytes().await?;
                Packet::SetClipboard {
                    id,
                    seq_num,
                    mark,
                    data,
                }
            }
            "DMUP" => {
                let id = self.stream.read_i8().await?;
                Packet::MouseUp { id }
            }
            "DMDN" => {
                let id = self.stream.read_i8().await?;
                Packet::MouseDown { id }
            }
            "DKUP" => {
                let id = self.stream.read_u16().await?;
                let mask = self.stream.read_u16().await?;
                let button = self.stream.read_u16().await?;
                Packet::KeyUp { id, mask, button }
            }
            "DKDN" => {
                let id = self.stream.read_u16().await?;
                let mask = self.stream.read_u16().await?;
                let button = self.stream.read_u16().await?;
                Packet::KeyDown { id, mask, button }
            }
            "DMWM" => {
                let x_delta = self.stream.read_i16().await?;
                let y_delta = self.stream.read_i16().await?;
                Packet::MouseWheel { x_delta, y_delta }
            }
            _ => {
                let mut s = size - 4;
                while s > 0 {
                    let _: [u8; 1] = self.stream.read_bytes_fixed().await?;
                    s = s - 1;
                }
                Packet::Unknown(code)
            }
        };

        Ok(packet)
    }

    pub async fn write(&mut self, packet: Packet) -> Result<(), PacketError> {
        packet.write_wire(&mut self.stream).await?;
        Ok(())
    }
}

#[derive(Debug)]
pub enum Packet {
    QueryInfo,
    DeviceInfo {
        x: u16,
        y: u16,
        w: u16,
        h: u16,
        dummy: u16,
        mx: u16, // x position of the mouse on the secondary screen
        my: u16, // y position of the mouse on the secondary screen
    },
    InfoAck,
    KeepAlive,
    ResetOptions,
    ClientNoOp,
    SetDeviceOptions(HashMap<String, u32>),
    ErrorUnknownDevice,
    GrabClipboard {
        id: u8,
        seq_num: u32,
    },
    SetClipboard {
        id: u8,
        seq_num: u32,
        mark: u8,
        data: Vec<u8>,
    },
    CursorEnter {
        x: u16,
        y: u16,
        seq_num: u32,
        mask: u16,
    },
    MouseUp {
        id: i8,
    },
    MouseDown {
        id: i8,
    },
    KeyUp {
        id: u16,
        mask: u16,
        button: u16,
    },
    KeyDown {
        id: u16,
        mask: u16,
        button: u16,
    },
    MouseWheel {
        x_delta: i16,
        y_delta: i16,
    },
    CursorLeave,
    MouseMoveAbs {
        x: u16,
        y: u16,
    },
    Unknown(String),
}

impl Packet {
    pub async fn write_wire<W: AsyncWrite + Send + Unpin>(
        self,
        mut out: W,
    ) -> Result<(), PacketError> {
        match self {
            Packet::QueryInfo => {
                out.write_str("QINF").await?;
                Ok(())
            }
            Packet::DeviceInfo {
                x,
                y,
                w,
                h,
                dummy: _dummy,
                mx,
                my,
            } => {
                out.write_u32(2 * 7 + 4).await?;
                out.write(b"DINF").await?;
                out.write_u16(x).await?;
                out.write_u16(y).await?;
                out.write_u16(w).await?;
                out.write_u16(h).await?;
                out.write_u16(0).await?;
                out.write_u16(mx).await?;
                out.write_u16(my).await?;
                Ok(())
            }
            Packet::ClientNoOp => {
                out.write_str("CNOP").await?;
                Ok(())
            }
            Packet::Unknown(_) => {
                unimplemented!()
            }
            Packet::InfoAck => {
                out.write_str("CIAK").await?;
                Ok(())
            }
            Packet::KeepAlive => {
                out.write_str("CALV").await?;
                Ok(())
            }
            Packet::ErrorUnknownDevice => {
                out.write_str("EUNK").await?;
                Ok(())
            }
            Packet::MouseMoveAbs { x, y } => {
                out.write_u32(4 + 2 + 2).await?;
                out.write(b"DMMV").await?;
                out.write_u16(x).await?;
                out.write_u16(y).await?;
                Ok(())
            }
            _ => {
                unimplemented!("{:?} not yet implemented", self)
            }
        }
    }
}

impl SynergyClient<TcpStream> {
    pub async fn connect(
        addr: &str,
        port: &str,
        baud: u32,
        device_name: &str,
        screen_size: (u16, u16),
    ) -> Result<Self, ConnectionError> {
        let mut port = serialport::new(port, baud)
            .parity(Parity::None)
            .data_bits(DataBits::Eight)
            .flow_control(FlowControl::None)
            .stop_bits(StopBits::One)
            .timeout(Duration::from_millis(1000))
            .open()
            .expect("Failed to open port");

        let mut stream = TcpStream::connect(addr).await?;
        // turn off Nagle
        stream.set_nodelay(true).unwrap();

        let _size = stream.read_packet_size().await?;
        stream.read_str_lit("Barrier").await?;
        let major = stream.read_u16().await?;
        let minor = stream.read_u16().await?;
        println!("got hello {major}:{minor}");

        stream
            .write_u32("Barrier".len() as u32 + 2 + 2 + 4 + device_name.bytes().len() as u32)
            .await?;
        stream.write(b"Barrier").await?;
        stream.write_u16(1).await?;
        stream.write_u16(6).await?;
        stream.write_str(device_name).await?;

        let mut packet_stream = PacketStream { stream };
        loop {
            while let Ok(packet) = packet_stream.read().await {
                match dbg!(packet) {
                    Packet::QueryInfo => {
                        packet_stream
                            .write(Packet::DeviceInfo {
                                x: 0,
                                y: 0,
                                w: screen_size.0,
                                h: screen_size.1,
                                dummy: 0,
                                mx: 0,
                                my: 0,
                            })
                            .await?;
                    }
                    Packet::KeepAlive => {
                        packet_stream.write(Packet::KeepAlive).await?;
                    }
                    Packet::MouseMoveAbs { x, y } => {
                        let abs_x = ((x as f32) * (0x7fff as f32 / 2560_f32)).ceil() as u16;
                        let abs_y = ((y as f32) * (0x7fff as f32 / 1600_f32)).ceil() as u16;
                        println!("Move mouse to {x} {y} {abs_x} {abs_y}");
                        let mut out = Vec::new();
                        byteorder_async::WriteBytesExt::write_u8(&mut out, 0_u8).unwrap();
                        byteorder_async::WriteBytesExt::write_u16::<BigEndian>(&mut out, abs_x)
                            .unwrap();
                        byteorder_async::WriteBytesExt::write_u16::<BigEndian>(&mut out, abs_y)
                            .unwrap();
                        port.write(&mut out).unwrap();
                    }
                    Packet::KeyUp { id, mask, button } => {
                        println!("Key up: {id} {mask} {button}");
                        port.write_u8(4).unwrap();
                        port.write_u16::<BigEndian>(id).unwrap();
                        port.write_u16::<BigEndian>(mask).unwrap();
                        port.write_u16::<BigEndian>(button).unwrap();
                    }
                    Packet::KeyDown { id, mask, button } => {
                        println!("Key down: {id} {mask} {button}");
                        port.write_u8(3).unwrap();
                        port.write_u16::<BigEndian>(id).unwrap();
                        port.write_u16::<BigEndian>(mask).unwrap();
                        port.write_u16::<BigEndian>(button).unwrap();
                    }
                    Packet::MouseDown { id } => {
                        println!("Mouse down: {id}");
                        port.write_u8(1).unwrap();
                        port.write_i8(id).unwrap();
                    }
                    Packet::MouseUp { id } => {
                        println!("Mouse up: {id}");
                        port.write_u8(2).unwrap();
                        port.write_i8(id).unwrap();
                    }
                    Packet::MouseWheel { x_delta, y_delta } => {
                        port.write_u8(5).unwrap();
                        port.write_i16::<BigEndian>(x_delta).unwrap();
                        port.write_i16::<BigEndian>(y_delta).unwrap();
                    }
                    Packet::InfoAck => { //Ignore
                    }
                    Packet::ResetOptions => {
                        // TODO: Nothing to reset yet
                    }
                    Packet::SetDeviceOptions(opts) => {
                        let mut opts: Vec<_> = opts.into_iter().collect();
                        opts.sort_by_key(|(k, _)| k.clone());
                        println!(
                            "server sent options: {}",
                            opts.into_iter()
                                .map(|(k, v)| format!("({k}, {v})"))
                                .collect::<Vec<_>>()
                                .join(" ")
                        )
                    }
                    Packet::CursorEnter { .. } => {
                        port.write_u8(6).unwrap();
                    }
                    Packet::CursorLeave => {
                        port.write_u8(7).unwrap();
                    }
                    Packet::GrabClipboard { .. } => {}
                    Packet::SetClipboard { .. } => {}
                    Packet::DeviceInfo { .. } | Packet::ErrorUnknownDevice | Packet::ClientNoOp => {
                        // Server only packets
                    }
                    Packet::Unknown(_) => {}
                }
            }
            break;
        }

        Ok(Self {
            _packet_stream: packet_stream,
        })
    }
}
