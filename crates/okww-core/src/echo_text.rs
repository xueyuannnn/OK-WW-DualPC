/// QString uses UTF-16. Keep unmatched code units, including unpaired
/// surrogates, intact instead of converting through a lossy UTF-8 string.
pub fn simplify(words: &mut [u16]) {
    let from = "聲強調諧階段擊傷禦鳴普輔鎖棄認數據構獲養當屬選擇點擊任處返還體效攻擊";
    let to = "声强调谐阶段击伤御鸣普辅锁弃认数据构获养当属选择点击任处返还体效攻击";
    for (before, after) in from.chars().zip(to.chars()) {
        for word in words.iter_mut() {
            if *word == before as u16 {
                *word = after as u16;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn preserves_non_bmp_and_unpaired_surrogates() {
        let mut text = [0xd83d, 0xde00, 0xd800, '聲' as u16, 0xdc00, '%' as u16];
        super::simplify(&mut text);
        assert_eq!(
            text,
            [0xd83d, 0xde00, 0xd800, '声' as u16, 0xdc00, '%' as u16]
        );
        super::simplify(&mut []);
    }
}
