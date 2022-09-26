use async_trait::async_trait;
use std::io;
use thiserror::Error;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

#[derive(Error, Debug)]
pub enum PacketError {
    #[error("io error")]
    IoError(#[from] io::Error),
    #[error("did not match format")]
    FormatError,
    #[error("not enough data")]
    InsufficientDataError,
    #[error("Packet too small")]
    PacketTooSmall,
}

#[async_trait]
pub trait PacketReader: AsyncRead + Send + Unpin {
    async fn read_packet_size(&mut self) -> Result<u32, PacketError> {
        Ok(self.read_u32().await?)
    }

    async fn read_bytes_fixed<const N: usize>(&mut self) -> Result<[u8; N], PacketError> {
        let mut res = [0; N];
        self.read(&mut res).await?;
        Ok(res)
    }

    async fn read_bytes(&mut self) -> Result<Vec<u8>, PacketError> {
        let mut buf = vec![];

        let len = self.read_u32().await?;

        let mut chunk =
            self.take(u64::try_from(len).map_err(|_| PacketError::InsufficientDataError)?);
        chunk.read_to_end(&mut buf).await?;

        Ok(buf)
    }

    async fn read_str_lit(&mut self, lit: &str) -> Result<(), PacketError> {
        let mut buf = vec![];

        let mut chunk =
            self.take(u64::try_from(lit.len()).map_err(|_| PacketError::InsufficientDataError)?);
        chunk.read_to_end(&mut buf).await?;

        if buf == lit.as_bytes() {
            Ok(())
        } else {
            Err(PacketError::FormatError)
        }
    }
}

impl<T: AsyncRead + Send + Unpin> PacketReader for T {}

#[async_trait]
pub trait PacketWriter: AsyncWrite + Send + Unpin {
    // async fn write_u32(&mut self, data: u32) -> Result<(), PacketError> {
    //     Ok(WriteBytesExt::write_u32::<BigEndian>(self, data)?)
    // }
    //
    // async fn write_u16(&mut self, data: u16) -> Result<(), PacketError> {
    //     Ok(WriteBytesExt::write_u16::<BigEndian>(self, data)?)
    // }

    async fn write_str(&mut self, data: &str) -> Result<(), PacketError> {
        self.write_u32(data.len() as u32).await?;
        self.write(data.as_bytes()).await?;
        Ok(())
    }
}

impl<T: AsyncWrite + Send + Unpin> PacketWriter for T {}
