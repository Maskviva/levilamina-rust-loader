use crate::error::{Error, Result};
use crate::server::dimsel::selector;
use crate::Server;

pub fn is_valid_name(name: &str) -> bool {
    !name.is_empty() && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn sel_or_err(dim: i32) -> Result<String> {
    selector(dim).ok_or_else(|| Error(format!("维度 {dim} 没有可用的选择器")))
}

impl Server {
    pub fn add_ticking_area(
        &self,
        dim: i32,
        from: (i32, i32),
        to: (i32, i32),
        name: &str,
    ) -> Result<()> {
        if !is_valid_name(name) {
            return Err(Error(format!(
                "常加载区块名「{name}」不合法：只能用 A-Z a-z 0-9 和下划线。"
            )));
        }
        let sel = sel_or_err(dim)?;
        let r = self.execute_command(&format!(
            "execute in {sel} run tickingarea add {} 0 {} {} 0 {} {name}",
            from.0, from.1, to.0, to.1
        ))?;
        if r.success {
            Ok(())
        } else {
            Err(Error(format!(
                "常加载区块「{name}」没能建立：{}",
                if r.output.is_empty() {
                    "引擎没有说明原因".into()
                } else {
                    r.output
                }
            )))
        }
    }

    pub fn remove_ticking_area(&self, dim: i32, name: &str) -> Result<()> {
        let sel = sel_or_err(dim)?;
        let r = self.execute_command(&format!("execute in {sel} run tickingarea remove {name}"))?;
        if r.success {
            Ok(())
        } else {
            Err(Error(format!("常加载区块「{name}」没能撤掉：{}", r.output)))
        }
    }

    pub fn list_ticking_areas(&self, dim: i32) -> Result<Vec<String>> {
        let sel = sel_or_err(dim)?;
        let r = self.execute_command(&format!("execute in {sel} run tickingarea list"))?;
        let mut out: Vec<String> = Vec::new();
        for tok in r
            .output
            .split(|c: char| !(c.is_ascii_alphanumeric() || c == '_'))
        {
            if tok.is_empty() || out.iter().any(|s| s == tok) {
                continue;
            }
            out.push(tok.to_string());
        }
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use crate::server::ticking::is_valid_name;

    #[test]
    fn only_command_safe_names_are_accepted() {
        assert!(is_valid_name("some_caller_1000_0_0"));
        assert!(is_valid_name("A9_"));

        assert!(!is_valid_name("plot 0,0"));
        assert!(!is_valid_name("a-b"));
        assert!(!is_valid_name("地皮"));
        assert!(!is_valid_name(""));
    }
}
