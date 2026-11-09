use anyhow::{Result, ensure};
use std::io::{Read, Seek, SeekFrom};

pub fn get_apk_signature(apk: &str) -> Result<(u32, String)> {
    let mut buffer = [0u8; 0x10];
    let mut size4 = [0u8; 4];
    let mut size8 = [0u8; 8];
    let mut size_of_block = [0u8; 8];

    let mut f = std::fs::File::open(apk)?;

    let mut i = 0;
    loop {
        let mut n = [0u8; 2];
        f.seek(SeekFrom::End(-i - 2))?;
        f.read_exact(&mut n)?;

        let n = u16::from_le_bytes(n);
        if i64::from(n) == i {
            f.seek(SeekFrom::Current(-22))?;
            f.read_exact(&mut size4)?;

            if u32::from_le_bytes(size4) ^ 0xcafe_babe_u32 == 0xccfb_f1ee_u32 {
                if i > 0 {
                    println!("warning: comment length is {i}");
                }
                break;
            }
        }

        ensure!(i < 0xffff, "not a zip file");

        i += 1;
    }

    f.seek(SeekFrom::Current(12))?;
    // Central-directory offset. The signing block footer (an 8-byte trailing
    // size followed by the 16-byte magic, 24 bytes in total) ends right before
    // it, so the footer must start at least 24 bytes before the CD.
    f.read_exact(&mut size4)?;
    let cd_offset = u64::from(u32::from_le_bytes(size4));
    let block_footer = cd_offset
        .checked_sub(0x18)
        .ok_or_else(|| anyhow::anyhow!("not a signed apk"))?;
    f.seek(SeekFrom::Start(block_footer))?;

    f.read_exact(&mut size8)?;
    f.read_exact(&mut buffer)?;

    ensure!(&buffer == b"APK Sig Block 42", "Can not found sig block");

    // The leading block-size field sits `trailing size + 8` bytes before the
    // CD. Use checked arithmetic so a malformed (too large / wrapping) size
    // returns an error instead of underflowing into a bogus seek offset.
    let block_head = u64::from_le_bytes(size8)
        .checked_add(0x8)
        .and_then(|span| cd_offset.checked_sub(span))
        .ok_or_else(|| anyhow::anyhow!("not a signed apk"))?;
    f.seek(SeekFrom::Start(block_head))?;
    f.read_exact(&mut size_of_block)?;

    ensure!(size_of_block == size8, "not a signed apk");

    let mut v2_signing: Option<(u32, String)> = None;
    let mut v3_signing_exist = false;
    let mut v3_1_signing_exist = false;

    loop {
        let mut id = [0u8; 4];
        let mut offset = 4u32;

        f.read_exact(&mut size8)?; // sequence length
        if size8 == size_of_block {
            break;
        }

        f.read_exact(&mut id)?; // id

        let id = u32::from_le_bytes(id);
        if id == 0x7109_871a_u32 {
            v2_signing = Some(calc_cert_sha256(&mut f, &mut size4, &mut offset)?);
        } else if id == 0xf053_68c0_u32 {
            // v3 signature scheme
            v3_signing_exist = true;
        } else if id == 0x1b93_ad61_u32 {
            // v3.1 signature scheme: credits to vvb2060
            v3_1_signing_exist = true;
        }

        f.seek(SeekFrom::Current(
            i64::from_le_bytes(size8) - i64::from(offset),
        ))?;
    }

    if v3_signing_exist || v3_1_signing_exist {
        return Err(anyhow::anyhow!("Unexpected v3 signature found!"));
    }

    v2_signing.ok_or_else(|| anyhow::anyhow!("No signature found!"))
}

fn calc_cert_sha256(
    f: &mut std::fs::File,
    size4: &mut [u8; 4],
    offset: &mut u32,
) -> Result<(u32, String)> {
    f.read_exact(size4)?; // signer-sequence length
    f.read_exact(size4)?; // signer length
    f.read_exact(size4)?; // signed data length
    *offset += 0x4 * 3;

    f.read_exact(size4)?; // digests-sequence length
    let pos = u32::from_le_bytes(*size4); // skip digests
    f.seek(SeekFrom::Current(i64::from(pos)))?;
    *offset += 0x4 + pos;

    f.read_exact(size4)?; // certificates length
    f.read_exact(size4)?; // certificate length
    *offset += 0x4 * 2;

    let cert_len = u32::from_le_bytes(*size4);
    let mut cert: Vec<u8> = vec![0; cert_len as usize];
    f.read_exact(&mut cert)?;
    *offset += cert_len;

    Ok((cert_len, sha256::digest(&cert)))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    const EOCD_SIGNATURE: u32 = 0x0605_4b50;
    const SIG_BLOCK_MAGIC: &[u8] = b"APK Sig Block 42";
    const SCHEME_V2: u32 = 0x7109_871a;
    const SCHEME_V3: u32 = 0xf053_68c0;
    const SCHEME_V3_1: u32 = 0x1b93_ad61;

    /// One APK Signing Block key-value pair: u64 pair-size, u32 id, value.
    fn signing_pair(id: u32, value: &[u8]) -> Vec<u8> {
        let mut pair = Vec::new();
        pair.extend_from_slice(&(u64::try_from(value.len()).unwrap() + 4).to_le_bytes());
        pair.extend_from_slice(&id.to_le_bytes());
        pair.extend_from_slice(value);
        pair
    }

    /// Minimal v2 pair value holding a single signer/certificate. Only the
    /// offsets walked by `calc_cert_sha256` are meaningful; the surrounding
    /// length fields are placeholders that the parser consumes but ignores.
    fn v2_pair_value(certificate: &[u8], digest_len: u32) -> Vec<u8> {
        let mut value = Vec::new();
        value.extend_from_slice(&0u32.to_le_bytes()); // signer-sequence length
        value.extend_from_slice(&0u32.to_le_bytes()); // first signer length
        value.extend_from_slice(&0u32.to_le_bytes()); // signed-data length
        value.extend_from_slice(&digest_len.to_le_bytes()); // digests-sequence length
        value.extend(std::iter::repeat_n(0xDBu8, digest_len as usize)); // digests
        value.extend_from_slice(&0u32.to_le_bytes()); // certificates-sequence length
        value.extend_from_slice(&u32::try_from(certificate.len()).unwrap().to_le_bytes()); // first certificate length
        value.extend_from_slice(certificate);
        value
    }

    /// Assemble `[signing block][empty central directory][EOCD][comment]`.
    fn build_apk(pairs: &[Vec<u8>], comment: &[u8]) -> Vec<u8> {
        let pairs_len: usize = pairs.iter().map(Vec::len).sum();
        // size_of_block excludes the leading size field and covers the
        // trailing size field (8 bytes) plus the 16-byte magic.
        let size_of_block = u64::try_from(pairs_len + 8 + 16).unwrap();

        let mut block = Vec::new();
        block.extend_from_slice(&size_of_block.to_le_bytes());
        for pair in pairs {
            block.extend_from_slice(pair);
        }
        block.extend_from_slice(&size_of_block.to_le_bytes());
        block.extend_from_slice(SIG_BLOCK_MAGIC);

        let mut eocd = Vec::new();
        eocd.extend_from_slice(&EOCD_SIGNATURE.to_le_bytes());
        eocd.extend_from_slice(&[0u8; 12]); // disk / cd-disk / entry counts / CD size
        eocd.extend_from_slice(&u32::try_from(block.len()).unwrap().to_le_bytes()); // CD offset
        eocd.extend_from_slice(&u16::try_from(comment.len()).unwrap().to_le_bytes()); // comment length

        let mut apk = block;
        apk.extend_from_slice(&eocd);
        apk.extend_from_slice(comment);
        apk
    }

    fn parse(apk: &[u8]) -> Result<(u32, String)> {
        let mut file = NamedTempFile::new().unwrap();
        file.write_all(apk).unwrap();
        get_apk_signature(file.path().to_str().unwrap())
    }

    #[test]
    fn extracts_v2_certificate_size_and_sha256() {
        let certificate: Vec<u8> = (0..32u8).map(|byte| byte ^ 0xC0).collect();
        let apk = build_apk(
            &[signing_pair(SCHEME_V2, &v2_pair_value(&certificate, 8))],
            &[],
        );

        let (size, hash) = parse(&apk).unwrap();
        assert_eq!(size, u32::try_from(certificate.len()).unwrap());
        assert_eq!(hash, sha256::digest(&certificate));
    }

    #[test]
    fn locates_eocd_behind_a_zip_comment() {
        let certificate = vec![0xA5u8; 24];
        let apk = build_apk(
            &[signing_pair(SCHEME_V2, &v2_pair_value(&certificate, 4))],
            b"this is a trailing zip comment",
        );

        let (size, hash) = parse(&apk).unwrap();
        assert_eq!(size, u32::try_from(certificate.len()).unwrap());
        assert_eq!(hash, sha256::digest(&certificate));
    }

    #[test]
    fn rejects_v3_signing_block() {
        let apk = build_apk(&[signing_pair(SCHEME_V3, &[1, 2, 3, 4])], &[]);
        let error = parse(&apk).unwrap_err().to_string();
        assert!(error.contains("v3 signature"), "unexpected error: {error}");
    }

    #[test]
    fn rejects_v3_1_signing_block() {
        let apk = build_apk(&[signing_pair(SCHEME_V3_1, &[1, 2, 3, 4])], &[]);
        let error = parse(&apk).unwrap_err().to_string();
        assert!(error.contains("v3 signature"), "unexpected error: {error}");
    }

    #[test]
    fn rejects_block_without_signature_pair() {
        let error = parse(&build_apk(&[], &[])).unwrap_err().to_string();
        assert!(error.contains("No signature"), "unexpected error: {error}");
    }

    #[test]
    fn rejects_bad_sig_block_magic() {
        let certificate = vec![0xA5u8; 16];
        let mut apk = build_apk(
            &[signing_pair(SCHEME_V2, &v2_pair_value(&certificate, 0))],
            &[],
        );
        // The 16-byte magic sits directly in front of the 22-byte EOCD.
        let magic_start = apk.len() - 22 - 16;
        for byte in &mut apk[magic_start..magic_start + 16] {
            *byte = 0;
        }
        let error = parse(&apk).unwrap_err().to_string();
        assert!(
            error.contains("Can not found sig block"),
            "unexpected error: {error}"
        );
    }

    #[test]
    fn rejects_mismatched_block_size() {
        let certificate = vec![0xA5u8; 16];
        let mut apk = build_apk(
            &[signing_pair(SCHEME_V2, &v2_pair_value(&certificate, 0))],
            &[],
        );
        apk[0..8].copy_from_slice(&0xDEAD_BEEFu64.to_le_bytes());
        let error = parse(&apk).unwrap_err().to_string();
        assert!(
            error.contains("not a signed apk"),
            "unexpected error: {error}"
        );
    }

    #[test]
    fn rejects_truncated_non_zip_without_panicking() {
        assert!(parse(b"not a zip").is_err());
        assert!(parse(&[]).is_err());
    }

    #[test]
    fn rejects_cd_offset_smaller_than_block_footer() {
        let certificate = vec![0xA5u8; 16];
        let mut apk = build_apk(
            &[signing_pair(SCHEME_V2, &v2_pair_value(&certificate, 0))],
            &[],
        );
        // In a comment-less EOCD the central-directory offset (u32) sits six
        // bytes before EOF (just ahead of the 2-byte comment length).
        let cd_off_idx = apk.len() - 6;
        // 10 < 24 (8-byte trailing block size + 16-byte magic): there is no
        // room for an APK Signing Block footer before the central directory.
        apk[cd_off_idx..cd_off_idx + 4].copy_from_slice(&10u32.to_le_bytes());
        let error = parse(&apk).unwrap_err().to_string();
        assert!(
            error.contains("not a signed apk"),
            "unexpected error: {error}"
        );
    }

    #[test]
    fn rejects_block_size_extending_past_cd_offset() {
        let certificate = vec![0xA5u8; 16];
        let mut apk = build_apk(
            &[signing_pair(SCHEME_V2, &v2_pair_value(&certificate, 0))],
            &[],
        );
        let cd_off_idx = apk.len() - 6;
        let cd_offset = u32::from_le_bytes(apk[cd_off_idx..cd_off_idx + 4].try_into().unwrap());
        // The trailing block-size (u64) starts 46 bytes before EOF in a
        // comment-less APK (22-byte EOCD + 16-byte magic + 8-byte size).
        let tail_idx = apk.len() - 46;
        // Claim a block that would extend before the central directory offset.
        let bogus = u64::from(cd_offset).saturating_add(0x100);
        apk[tail_idx..tail_idx + 8].copy_from_slice(&bogus.to_le_bytes());
        let error = parse(&apk).unwrap_err().to_string();
        assert!(
            error.contains("not a signed apk"),
            "unexpected error: {error}"
        );
    }
}
