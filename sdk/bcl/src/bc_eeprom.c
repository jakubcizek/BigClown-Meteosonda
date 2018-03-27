#include <bc_eeprom.h>
#include <bc_irq.h>
#include <stm32l0xx.h>
#include <bc_tick.h>

static bool _bc_eeprom_is_busy(bc_tick_t timeout);

bool bc_eeprom_write(uint32_t address, const void *buffer, size_t length)
{
    // Add EEPROM base offset to address
    address += DATA_EEPROM_BASE;

    // If user attempts to write outside EEPROM area...
    if ((address + length) > (DATA_EEPROM_BANK2_END + 1))
    {
        // Indicate failure
        return false;
    }

    if (_bc_eeprom_is_busy(50))
    {
        return false;
    }

    // Disable interrupts
    bc_irq_disable();

    // Unlock FLASH_PECR register
    if ((FLASH->PECR & FLASH_PECR_PELOCK) != 0)
    {
        FLASH->PEKEYR = FLASH_PEKEY1;
        FLASH->PEKEYR = FLASH_PEKEY2;
    }

    // Enable interrupts
    bc_irq_enable();

    // For every byte in buffer
    for (size_t i = 0UL; i < length; i++)
    {
        // Program data to address
        ((uint8_t *) address)[i] = ((uint8_t *) buffer)[i];

        while ((FLASH->SR & FLASH_SR_BSY) != 0UL)
        {
            continue;
        }
    }

    if (_bc_eeprom_is_busy(10 * length))
    {
        return false;
    }

    // Disable interrupts
    bc_irq_disable();

    // Lock FLASH_PECR register
    FLASH->PECR |= FLASH_PECR_PELOCK;

    // Enable interrupts
    bc_irq_enable();

    // If we do not read what we wrote...
    if (memcmp(buffer, (void *) address, length) != 0UL)
    {
        // Indicate failure
        return false;
    }

    // Indicate success
    return true;
}

bool bc_eeprom_read(uint32_t address, void *buffer, size_t length)
{
    // Add EEPROM base offset to address
    address += DATA_EEPROM_BASE;

    // If user attempts to read outside of EEPROM boundary...
    if ((address + length) > (DATA_EEPROM_BANK2_END + 1))
    {
        // Indicate failure
        return false;
    }

    // Read from EEPROM memory to buffer
    memcpy(buffer, (void *) address, length);

    // Indicate success
    return true;
}

size_t bc_eeprom_get_size(void)
{
    // Return EEPROM memory size
    return DATA_EEPROM_BANK2_END - DATA_EEPROM_BASE + 1;
}

static bool _bc_eeprom_is_busy(bc_tick_t timeout)
{
    timeout += bc_tick_get();

    while ((FLASH->SR & FLASH_SR_BSY) != 0UL)
    {
        if (timeout > bc_tick_get())
        {
            return true;
        }
    }

    return false;
}
