use crate::elf::ElfFile;
use crate::util::{mask, mb, round_up};

const PAGE_TABLE_SIZE: usize = 4096;

const AARCH64_1GB_BLOCK_BITS: u64 = 30;
const AARCH64_2MB_BLOCK_BITS: u64 = 21;

const AARCH64_LVL0_BITS: u64 = 9;
const AARCH64_LVL1_BITS: u64 = 9;
const AARCH64_LVL2_BITS: u64 = 9;

struct Aarch64;
impl Aarch64 {
    pub fn lvl0_index(addr: u64) -> usize {
        let idx = (addr >> (AARCH64_2MB_BLOCK_BITS + AARCH64_LVL2_BITS + AARCH64_LVL1_BITS))
            & mask(AARCH64_LVL0_BITS);
        idx as usize
    }

    pub fn lvl1_index(addr: u64) -> usize {
        let idx = (addr >> (AARCH64_2MB_BLOCK_BITS + AARCH64_LVL2_BITS)) & mask(AARCH64_LVL1_BITS);
        idx as usize
    }

    pub fn lvl2_index(addr: u64) -> usize {
        let idx = (addr >> (AARCH64_2MB_BLOCK_BITS)) & mask(AARCH64_LVL2_BITS);
        idx as usize
    }
}

pub struct CoreManager<'a> {
    target_elf: &'a mut ElfFile,
    kernel_elf: &'a ElfFile,
    pagetable_vars: Vec<(u64, u64, [u8; 4096])>
}

impl<'a> CoreManager<'a> {
    pub fn new(kernel_elf: &'a ElfFile, target_elf: &'a mut ElfFile) -> CoreManager<'a> {
        let mut kernel_first_vaddr = None;
        let mut kernel_last_vaddr = None;
        let mut kernel_first_paddr = None;
        let mut kernel_p_v_offset = None;

        for segment in &kernel_elf.segments {
            if segment.loadable {
                if kernel_first_vaddr.is_none() || segment.virt_addr < kernel_first_vaddr.unwrap() {
                    kernel_first_vaddr = Some(segment.virt_addr);
                }

                if kernel_last_vaddr.is_none()
                    || segment.virt_addr + segment.mem_size() > kernel_last_vaddr.unwrap()
                {
                    kernel_last_vaddr =
                        Some(round_up(segment.virt_addr + segment.mem_size(), mb(2)));
                }

                if kernel_first_paddr.is_none() || segment.phys_addr < kernel_first_paddr.unwrap() {
                    kernel_first_paddr = Some(segment.phys_addr);
                }

                if kernel_p_v_offset.is_none() {
                    kernel_p_v_offset = Some(segment.virt_addr - segment.phys_addr);
                } else if kernel_p_v_offset.unwrap() != segment.virt_addr - segment.phys_addr {
                    panic!("Kernel does not have a consistent physical to virtual offset");
                }
            }
        }

        assert!(kernel_first_vaddr.is_some());
        assert!(kernel_first_paddr.is_some());

        let pagetable_vars = CoreManager::aarch64_setup_pagetables(
            target_elf,
            kernel_first_vaddr.unwrap(),
            kernel_first_paddr.unwrap(),
        );

        CoreManager {
            target_elf,
            kernel_elf,
            pagetable_vars
        }
    }

    pub fn patch_elf(&mut self) -> Result<(), String> {
        self.target_elf.write_symbol("boot_lvl0_lower", &self.pagetable_vars[0].2)?;
        self.target_elf.write_symbol("boot_lvl1_lower", &self.pagetable_vars[1].2)?;
        self.target_elf.write_symbol("boot_lvl0_upper", &self.pagetable_vars[2].2)?;
        self.target_elf.write_symbol("boot_lvl1_upper", &self.pagetable_vars[3].2)?;
        self.target_elf.write_symbol("boot_lvl2_upper", &self.pagetable_vars[4].2)?;

        self.target_elf.write_symbol("kernel_entry", &self.kernel_elf.entry.to_le_bytes())?;

        let (vaddr, size) = self.target_elf.find_symbol("boot_lvl0_lower")?;
        if let Some(data) = self.target_elf.get_data(vaddr, size) {
            println!("Boot L0 lower first 16 bytes: {:02x?}", &data[..16]);
        } else {
            println!("Failed to get data for boot_lvl0_lower");
        }

        Ok(())
    }

    pub fn aarch64_setup_pagetables(
        elf: &ElfFile,
        first_vaddr: u64,
        first_paddr: u64,
    ) -> Vec<(u64, u64, [u8; PAGE_TABLE_SIZE])> {
        let (mut boot_lvl1_lower_addr, boot_lvl1_lower_size) = elf
            .find_symbol("boot_lvl1_lower")
            .expect("Could not find 'boot_lvl1_lower' symbol");
        let (mut boot_lvl1_upper_addr, boot_lvl1_upper_size) = elf
            .find_symbol("boot_lvl1_upper")
            .expect("Could not find 'boot_lvl1_upper' symbol");
        let (mut boot_lvl2_upper_addr, boot_lvl2_upper_size) = elf
            .find_symbol("boot_lvl2_upper")
            .expect("Could not find 'boot_lvl2_upper' symbol");
        let (mut boot_lvl0_lower_addr, boot_lvl0_lower_size) = elf
            .find_symbol("boot_lvl0_lower")
            .expect("Could not find 'boot_lvl0_lower' symbol");
        let (mut boot_lvl0_upper_addr, boot_lvl0_upper_size) = elf
            .find_symbol("boot_lvl0_upper")
            .expect("Could not find 'boot_lvl0_upper' symbol");

        // Patch the addresses to be what the core manager would see in physical bootstrap memory
        let (bootstrap_start_vaddr, _) = elf
            .find_symbol("bootstrap_start")
            .expect("Could not find 'bootstrap_start' symbol");
        boot_lvl1_lower_addr = (boot_lvl1_lower_addr - bootstrap_start_vaddr) | 0x80000000;
        boot_lvl1_upper_addr = (boot_lvl1_upper_addr - bootstrap_start_vaddr) | 0x80000000;
        boot_lvl2_upper_addr = (boot_lvl2_upper_addr - bootstrap_start_vaddr) | 0x80000000;
        boot_lvl0_lower_addr = (boot_lvl0_lower_addr - bootstrap_start_vaddr) | 0x80000000;
        boot_lvl0_upper_addr = (boot_lvl0_upper_addr - bootstrap_start_vaddr) | 0x80000000;

        // Continue with normal page table setting

        let mut boot_lvl0_lower: [u8; PAGE_TABLE_SIZE] = [0; PAGE_TABLE_SIZE];
        boot_lvl0_lower[..8].copy_from_slice(&(boot_lvl1_lower_addr | 3).to_le_bytes());

        let mut boot_lvl1_lower: [u8; PAGE_TABLE_SIZE] = [0; PAGE_TABLE_SIZE];
        for i in 0..512 {
            #[allow(clippy::identity_op)] // keep the (0 << 2) for clarity
            let pt_entry: u64 = ((i as u64) << AARCH64_1GB_BLOCK_BITS) |
                (1 << 10) | // access flag
                (0 << 2) | // strongly ordered memory
                (1); // 1G block
            let start = 8 * i;
            let end = 8 * (i + 1);
            boot_lvl1_lower[start..end].copy_from_slice(&pt_entry.to_le_bytes());
        }

        let boot_lvl0_upper: [u8; PAGE_TABLE_SIZE] = [0; PAGE_TABLE_SIZE];
        {
            let pt_entry = (boot_lvl1_upper_addr | 3).to_le_bytes();
            let idx = Aarch64::lvl0_index(first_vaddr);
            boot_lvl0_lower[8 * idx..8 * (idx + 1)].copy_from_slice(&pt_entry);
        }

        let mut boot_lvl1_upper: [u8; PAGE_TABLE_SIZE] = [0; PAGE_TABLE_SIZE];
        {
            let pt_entry = (boot_lvl2_upper_addr | 3).to_le_bytes();
            let idx = Aarch64::lvl1_index(first_vaddr);
            boot_lvl1_upper[8 * idx..8 * (idx + 1)].copy_from_slice(&pt_entry);
        }

        let mut boot_lvl2_upper: [u8; PAGE_TABLE_SIZE] = [0; PAGE_TABLE_SIZE];

        let lvl2_idx = Aarch64::lvl2_index(first_vaddr);
        for i in lvl2_idx..512 {
            let entry_idx = (i - Aarch64::lvl2_index(first_vaddr)) << AARCH64_2MB_BLOCK_BITS;
            let pt_entry: u64 = (entry_idx as u64 + first_paddr) |
                (1 << 10) | // Access flag
                (3 << 8) | // Make sure the shareability is the same as the kernel's
                (4 << 2) | // MT_NORMAL memory
                (1 << 0); // 2MB block
            let start = 8 * i;
            let end = 8 * (i + 1);
            boot_lvl2_upper[start..end].copy_from_slice(&pt_entry.to_le_bytes());
        }

        vec![
            (boot_lvl0_lower_addr, boot_lvl0_lower_size, boot_lvl0_lower),
            (boot_lvl1_lower_addr, boot_lvl1_lower_size, boot_lvl1_lower),
            (boot_lvl0_upper_addr, boot_lvl0_upper_size, boot_lvl0_upper),
            (boot_lvl1_upper_addr, boot_lvl1_upper_size, boot_lvl1_upper),
            (boot_lvl2_upper_addr, boot_lvl2_upper_size, boot_lvl2_upper),
        ]
    }
}
