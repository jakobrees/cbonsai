# cbonsai(1) completion

_cbonsai()
{
  local cur prev opts
  _get_comp_words_by_ref cur prev

  opts=(
    '-l'
    '--live'
    '-t'
    '--time'
    '-i'
    '--infinite'
    '-w'
    '--wait'
    '-S'
    '--screensaver'
    '-m'
    '--message'
    '-T'
    '--msgtime'
    '-P'
    '--procedural'
    '-N'
    '--name'
    '-b'
    '--base'
    '-c'
    '--leaf'
    '-M'
    '--multiplier'
    '-L'
    '--life'
    '-p'
    '--print'
    '-s'
    '--seed'
    '--engine'
    '--bare'
    '--season'
    '--falling-leaves'
    '--falling-rate'
    '--falling-speed'
    '--falling-seasons'
    '-W'
    '--save'
    '-C'
    '--load'
    '-v'
    '--verbose'
    '--version'
    '-h'
    '--help'
  )

  case "$prev" in
    -[WC]|--save|--load)
      COMPREPLY=($(compgen -f -- "$cur"))
      return
      ;;
    --engine)
      COMPREPLY=($(compgen -W "1 2" -- "$cur"))
      return
      ;;
    --falling-leaves)
      COMPREPLY=($(compgen -W "dots lines ${opts[*]}" -- "$cur"))
      return
      ;;
    --falling-rate|--falling-speed)
      return
      ;;
    --falling-seasons)
      COMPREPLY=($(compgen -W "all spring summer autumn early-autumn late-autumn winter" -- "$cur"))
      return
      ;;
    --season)
      COMPREPLY=($(compgen -W "spring summer autumn late-autumn winter" -- "$cur"))
      return
      ;;
    -[twmTNbcMLs]|--time|--wait|--message|--msgtime|--name|--base|--leaf|--multiplier|--life|--seed)
      return
      ;;
  esac

  COMPREPLY=($(compgen -W "${opts[*]}" -- "$cur"))

} &&
complete -F _cbonsai cbonsai
