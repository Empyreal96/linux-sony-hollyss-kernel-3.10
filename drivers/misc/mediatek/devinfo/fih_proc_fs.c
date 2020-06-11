/*
* Copyright(C) 2011-2015 Foxconn International Holdings, Ltd. All rights reserved
*/
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/seq_file.h>

/*Header file which defined by FIH*/
#include <linux/fih_hw_info.h>
#include <linux/version_host.h>
#include <linux/version_nonhlos.h>
/***********************************************************
 *  Implement specific File operations for each proc entry *
 ***********************************************************/

/*-------------------------------
   Show the Device Model information
  --------------------------------*/
static int devmodel_proc_show(struct seq_file *m, void *v)
{
	int i;
	unsigned int project = fih_get_product_id();
	char ver[16]= {0} ;
	
	for(i=0; project_id_map[i].ID != PROJECT_MAX; i++)
	{
	  if(project_id_map[i].ID == project)
	  {
		strncpy(ver, project_id_map[i].STR ,project_id_map[i].LEN);
		ver[project_id_map[i].LEN]='\0';
		seq_printf(m, "%s\n", ver);
		break;
	  }
	}

	return 0;
}
static int devmodel_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, devmodel_proc_show, NULL);
}


/*---------------------------------
   Show the Device Phase ID information
  ----------------------------------*/

static int phaseid_read_proc_show(struct seq_file *m, void *v)
{
	int i;
	int phase = fih_get_product_phase();
	char ver[16]= {0};

	for(i=0; phase_id_map[i].ID != PHASE_MAX; i++)
	{
	  if(phase_id_map[i].ID == phase)
	  {
		strncpy(ver, phase_id_map[i].STR ,phase_id_map[i].LEN);
		ver[phase_id_map[i].LEN]='\0';
		seq_printf(m, "%s\n", ver);
		break;
	  }
	}

	return 0;
}
static int phaseid_read_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, phaseid_read_proc_show, NULL);
}


/*-------------------------------
    Show the Device BAND information
   -------------------------------*/
static int bandinfo_read_proc_show(struct seq_file *m, void *v)
{
	int i;
	int band = fih_get_band_id();
	char ver[16]= {0};

	for(i=0; band_id_map[i].ID != BAND_MAX; i++)
	{
	  if(band_id_map[i].ID == band)
	  {
		strncpy(ver, band_id_map[i].STR ,band_id_map[i].LEN);
		ver[band_id_map[i].LEN]='\0';
		seq_printf(m, "%s\n", ver);
		break;
	  }
	}

	return 0;
}
static int bandinfo_read_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, bandinfo_read_proc_show, NULL);
}


/*------------------------------
    Show the Device SIM information
   ------------------------------*/
static int siminfo_read_proc_show(struct seq_file *m, void *v)
{
	int i;
	int sim = fih_get_sim_id();
	char ver[16]= {0} ;
	
	for(i=0; sim_id_map[i].ID != SIM_MAX; i++)
	{
	  if(sim_id_map[i].ID == sim)
	  {
		strncpy(ver, sim_id_map[i].STR ,sim_id_map[i].LEN);
		ver[sim_id_map[i].LEN]='\0';
		seq_printf(m, "%s\n", ver);
		break;
	  }
	}

	return 0;
}
static int siminfo_read_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, siminfo_read_proc_show, NULL);
}



/*---------------------------
    Show the Non-HLOS versioin 
   ---------------------------*/
static int nonHLOS_ver_read_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s.%s.%s.%s\n",
		       VER_NONHLOS_BSP_VERSION,
			   VER_NONHLOS_PLATFORM_NUMBER,
			   VER_NONHLOS_BRANCH_NUMBER,
			   VER_NONHLOS_BUILD_NUMBER);

	return 0;
}
static int nonHLOS_ver_read_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nonHLOS_ver_read_proc_show, NULL);
}



/*-----------------------
  Show the HLOS versioin
-----------------------*/

static int HLOS_ver_read_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s.%s.%s.%s\n",
		       VER_HOST_BSP_VERSION,
			   VER_HOST_PLATFORM_NUMBER,
			   VER_HOST_BRANCH_NUMBER,
			   VER_HOST_BUILD_NUMBER);

	return 0;

}
static int HLOS_ver_read_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, HLOS_ver_read_proc_show, NULL);
}



/*-------------------------------------
    Show the Non-HLOS git head information
   -------------------------------------*/
static int nonHLOS_githd_read_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", VER_NONHLOS_GIT_COMMIT);

	return 0;
}
static int nonHLOS_githd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nonHLOS_githd_read_proc_show, NULL);
}



/*---------------------------------
 Show the HLOS git head information 
---------------------------------*/
static int HLOS_githd_read_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", VER_HOST_GIT_COMMIT);

	return 0;
}
static int HLOS_githd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, HLOS_githd_read_proc_show, NULL);
}



/*********************************
* Definition of File operations  *
**********************************/

static const struct file_operations fih_devmodel_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= devmodel_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_phaseid_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= phaseid_read_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_bandinfo_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= bandinfo_read_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_siminfo_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= siminfo_read_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_nonHLOS_ver_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= nonHLOS_ver_read_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_HLOS_ver_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= HLOS_ver_read_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_nonHLOS_githd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= nonHLOS_githd_proc_open,
	.read		= seq_read,
};

static const struct file_operations fih_HLOS_githd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= HLOS_githd_proc_open,
	.read		= seq_read,
};

/***************************
 * Init FIH's PROC enrty   *
 ***************************/
void fih_proc_init(void)
{
	/* Device Model */
	proc_create_data("devmodel", 0, NULL, &fih_devmodel_proc_fops, NULL);
	
	/* Device Phase ID */
	proc_create_data("phaseid", 0, NULL, &fih_phaseid_proc_fops, NULL);

	/* Band Information */
	proc_create_data("bandinfo", 0, NULL, &fih_bandinfo_proc_fops, NULL);

	/* SIM Information */
	proc_create_data("siminfo", 0, NULL, &fih_siminfo_proc_fops, NULL);

	/* NON-HLOS image Version */
	proc_create_data("nonHLOS_ver", 0, NULL, &fih_nonHLOS_ver_proc_fops, NULL);

	/* HLOS image Version */
	proc_create_data("HLOS_ver", 0, NULL, &fih_HLOS_ver_proc_fops, NULL);

	/* NON-HLOS git haed number */
	proc_create_data("nonHLOS_git_head", 0, NULL, &fih_nonHLOS_githd_proc_fops, NULL);

	/* HLOS git haed number */
	proc_create_data("HLOS_git_head", 0, NULL, &fih_HLOS_githd_proc_fops, NULL);
}
EXPORT_SYMBOL(fih_proc_init);
